#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include "common.h"
#include "shared_memory.h"

// Biến toàn cục cho shared memory
SharedMemorySegment *shared_mem = NULL;
void *shm_pointer = NULL;
int shmid = -1;

// Gửi phản hồi đến tab
void send_response(BrowserMessage *response) {
    char path[64];
    snprintf(path, sizeof(path), "%s%d", RESPONSE_FIFO_PREFIX, response->tab_id);
    int fd = open(path, O_WRONLY);
    if (fd >= 0) {
        write(fd, response, sizeof(BrowserMessage));
        close(fd);
    } else {
        perror("open response fifo");
    }
}

// Gửi thông điệp đến renderer
void send_to_renderer(BrowserMessage *msg) {
    int fd = open(RENDERER_FIFO, O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, sizeof(BrowserMessage));
        close(fd);
    } else {
        perror("open renderer fifo");
    }
}

// Gửi thông điệp đến resource manager
void send_to_resource_manager(BrowserMessage *msg) {
    int fd = open(RESOURCE_FIFO, O_WRONLY);
    if (fd >= 0) {
        write(fd, msg, sizeof(BrowserMessage));
        close(fd);
    } else {
        perror("open resource fifo");
    }
}

// Hàm render HTML thông qua w3m (legacy - sẽ dùng renderer process)
void render_html_with_w3m(const char *html_file, char *output) {
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "w3m -dump %s > /tmp/rendered.txt", html_file);
    system(cmd);

    FILE *fp = fopen("/tmp/rendered.txt", "r");
    if (!fp) {
        strcpy(output, "[Browser] Error: Cannot open rendered output.");
        return;
    }

    char line[256];
    output[0] = '\0';
    while (fgets(line, sizeof(line), fp)) {
        if (strlen(output) + strlen(line) < MAX_MSG - 1)
            strcat(output, line);
    }
    fclose(fp);
}

// Xử lý lệnh từ tab
void handle_command(BrowserMessage *msg) {
    BrowserMessage response;
    response.tab_id = msg->tab_id;
    response.type = MSG_ERROR;
    response.has_shared_data = 0;

    if (strncmp(msg->command, "load ", 5) == 0) {
        char html_file[MAX_MSG];
        char url[MAX_MSG];
        strcpy(url, msg->command + 5);
        snprintf(html_file, sizeof(html_file), "%s.html", url);

        FILE *check = fopen(html_file, "r");
        if (!check) {
            strcpy(response.command, "[Browser] Error: Page not found.");
            send_response(&response);
            return;
        }

        // Đọc nội dung file HTML
        fseek(check, 0, SEEK_END);
        long file_size = ftell(check);
        fseek(check, 0, SEEK_SET);

        char *html_content = (char *)malloc(file_size + 1);
        if (!html_content) {
            fclose(check);
            strcpy(response.command, "[Browser] Error: Memory allocation failed.");
            send_response(&response);
            return;
        }

        fread(html_content, 1, file_size, check);
        html_content[file_size] = '\0';
        fclose(check);

        // Lưu nội dung vào shared memory
        if (store_data_in_shared_memory(shared_mem, msg->tab_id, url, html_content, file_size) != 0) {
            strcpy(response.command, "[Browser] Error: Failed to store in shared memory.");
            send_response(&response);
            free(html_content);
            return;
        }

        free(html_content);

        // Gửi thông điệp đến renderer để render trang
        BrowserMessage renderer_msg;
        renderer_msg.tab_id = msg->tab_id;
        renderer_msg.type = MSG_RENDER_CONTENT;
        renderer_msg.has_shared_data = 1;
        strcpy(renderer_msg.command, url);
        send_to_renderer(&renderer_msg);

        // Thông báo resource manager để cache
        BrowserMessage resource_msg;
        resource_msg.tab_id = msg->tab_id;
        resource_msg.type = MSG_LOAD_PAGE;
        resource_msg.has_shared_data = 1;
        strcpy(resource_msg.command, url);
        send_to_resource_manager(&resource_msg);

        // Phản hồi tạm thời
        strcpy(response.command, "[Browser] Page loading...");
        response.type = MSG_PAGE_LOADED;
        send_response(&response);
    }
    else if (strcmp(msg->command, "reload") == 0) {
        strcpy(response.command, "[Browser] Reloading page...");
        response.type = MSG_RELOAD;
        send_response(&response);
    }
    else if (strcmp(msg->command, "back") == 0) {
        strcpy(response.command, "[Browser] Going back...");
        response.type = MSG_BACK;
        send_response(&response);
    }
    else if (strcmp(msg->command, "forward") == 0) {
        strcpy(response.command, "[Browser] Going forward...");
        response.type = MSG_FORWARD;
        send_response(&response);
    }
    else if (strcmp(msg->command, "CRASH") == 0) {
        strcpy(response.command, "[Browser] Tab crashed and recovered.");
        send_response(&response);
    }
    else {
        strcpy(response.command, "[Browser] Unknown command.");
        send_response(&response);
    }
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("[Browser] Received signal %d, cleaning up...\n", sig);
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    unlink(BROWSER_FIFO);
    exit(0);
}

int main() {
    int fd;
    BrowserMessage msg;

    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Tạo FIFO
    mkfifo(BROWSER_FIFO, 0666);
    printf("[Browser] Listening on %s...\n", BROWSER_FIFO);

    // Khởi tạo shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;

    while (1) {
        fd = open(BROWSER_FIFO, O_RDONLY);
        if (fd < 0) {
            perror("open browser fifo");
            continue;
        }

        while (read(fd, &msg, sizeof(msg)) > 0) {
            printf("[Browser] Tab %d sent: %s (type %d)\n", msg.tab_id, msg.command, msg.type);
            handle_command(&msg);
        }

        close(fd);
    }

    return 0;
}

