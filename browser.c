#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include "common.h"
#include "shared_memory.h"
#include "message_queue.h"
#include "semaphore.h"

// Biến toàn cục cho shared memory
SharedMemorySegment *shared_mem = NULL;
void *shm_pointer = NULL;
int shmid = -1;

// Biến toàn cục cho message queue
int browser_queue_id = -1;
int renderer_queue_id = -1;
int resource_queue_id = -1;
int *tab_queue_ids = NULL;
int max_tabs = 10;

// Biến toàn cục cho semaphore
int semid = -1;

// Gửi phản hồi đến tab qua message queue
void send_response_to_tab(BrowserMessage *response) {
    int tab_id = response->tab_id;
    
    // Tìm hoặc tạo message queue cho tab
    if (tab_id >= max_tabs || tab_queue_ids[tab_id] < 0) {
        key_t key = get_tab_queue_key(tab_id);
        tab_queue_ids[tab_id] = create_message_queue(key);
        if (tab_queue_ids[tab_id] < 0) {
            fprintf(stderr, "Failed to create message queue for tab %d\n", tab_id);
            return;
        }
        printf("[Browser] Created message queue for Tab %d with ID: %d\n", 
               tab_id, tab_queue_ids[tab_id]);
    }
    
    // Tạo message để gửi
    MessageQueueData msg_data;
    msg_data.mtype = 1; // Loại mặc định
    memcpy(&msg_data.message, response, sizeof(BrowserMessage));
    
    // Gửi message
    if (send_message(tab_queue_ids[tab_id], &msg_data, sizeof(BrowserMessage)) < 0) {
        fprintf(stderr, "Failed to send message to Tab %d\n", tab_id);
    } else {
        printf("[Browser] Sent response to Tab %d\n", tab_id);
    }
}

// Gửi thông điệp đến renderer qua message queue
void send_to_renderer(BrowserMessage *msg) {
    MessageQueueData msg_data;
    msg_data.mtype = 1; // Loại mặc định
    memcpy(&msg_data.message, msg, sizeof(BrowserMessage));
    
    if (send_message(renderer_queue_id, &msg_data, sizeof(BrowserMessage)) < 0) {
        fprintf(stderr, "Failed to send message to Renderer\n");
    } else {
        printf("[Browser] Sent message to Renderer\n");
    }
}

// Gửi thông điệp đến resource manager qua message queue
void send_to_resource_manager(BrowserMessage *msg) {
    MessageQueueData msg_data;
    msg_data.mtype = 1; // Loại mặc định
    memcpy(&msg_data.message, msg, sizeof(BrowserMessage));
    
    if (send_message(resource_queue_id, &msg_data, sizeof(BrowserMessage)) < 0) {
        fprintf(stderr, "Failed to send message to Resource Manager\n");
    } else {
        printf("[Browser] Sent message to Resource Manager\n");
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
        char url[MAX_MSG - 6]; // Để chừa chỗ cho ".html" và null terminator
        strncpy(url, msg->command + 5, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
        snprintf(html_file, sizeof(html_file), "%s.html", url);

        FILE *check = fopen(html_file, "r");
        if (!check) {
            strcpy(response.command, "[Browser] Error: Page not found.");
            send_response_to_tab(&response);
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
            send_response_to_tab(&response);
            return;
        }

        fread(html_content, 1, file_size, check);
        html_content[file_size] = '\0';
        fclose(check);

        // Lock semaphore trước khi truy cập shared memory
        lock_semaphore(semid, SEM_SHARED_MEM);
        
        // Lưu nội dung vào shared memory
        if (store_data_in_shared_memory(shared_mem, msg->tab_id, url, html_content, file_size) != 0) {
            unlock_semaphore(semid, SEM_SHARED_MEM);
            strcpy(response.command, "[Browser] Error: Failed to store in shared memory.");
            send_response_to_tab(&response);
            free(html_content);
            return;
        }
        
        // Unlock shared memory
        unlock_semaphore(semid, SEM_SHARED_MEM);
        
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
        send_response_to_tab(&response);
    }
    else if (strcmp(msg->command, "reload") == 0) {
        strcpy(response.command, "[Browser] Reloading page...");
        response.type = MSG_RELOAD;
        send_response_to_tab(&response);
    }
    else if (strcmp(msg->command, "back") == 0) {
        strcpy(response.command, "[Browser] Going back...");
        response.type = MSG_BACK;
        send_response_to_tab(&response);
    }
    else if (strcmp(msg->command, "forward") == 0) {
        strcpy(response.command, "[Browser] Going forward...");
        response.type = MSG_FORWARD;
        send_response_to_tab(&response);
    }
    else if (strcmp(msg->command, "CRASH") == 0) {
        strcpy(response.command, "[Browser] Tab crashed and recovered.");
        send_response_to_tab(&response);
    }
    else {
        strcpy(response.command, "[Browser] Unknown command.");
        send_response_to_tab(&response);
    }
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("[Browser] Received signal %d, cleaning up...\n", sig);
    
    // Hủy shared memory
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    
    // Hủy FIFO
    unlink(BROWSER_FIFO);
    
    // Hủy message queues
    if (browser_queue_id >= 0) {
        destroy_message_queue(browser_queue_id);
    }
    
    // Hủy các message queue cho tab
    if (tab_queue_ids) {
        for (int i = 0; i < max_tabs; i++) {
            if (tab_queue_ids[i] >= 0) {
                destroy_message_queue(tab_queue_ids[i]);
            }
        }
        free(tab_queue_ids);
    }
    
    // Hủy semaphore set
    if (semid >= 0) {
        destroy_semaphore(semid);
    }
    
    exit(0);
}

int main() {
    int fd;
    BrowserMessage msg;
    MessageQueueData msg_data;

    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Tạo FIFO (vẫn giữ lại tạm thời để tương thích ngược)
    mkfifo(BROWSER_FIFO, 0666);
    printf("[Browser] Legacy FIFO created at %s...\n", BROWSER_FIFO);

    // Khởi tạo shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;

    // Khởi tạo message queues
    browser_queue_id = create_message_queue(BROWSER_QUEUE_KEY);
    if (browser_queue_id < 0) {
        fprintf(stderr, "Failed to create browser message queue\n");
        signal_handler(SIGTERM);
        return 1;
    }
    
    renderer_queue_id = create_message_queue(RENDERER_QUEUE_KEY);
    if (renderer_queue_id < 0) {
        fprintf(stderr, "Failed to create renderer message queue\n");
        signal_handler(SIGTERM);
        return 1;
    }
    
    resource_queue_id = create_message_queue(RESOURCE_QUEUE_KEY);
    if (resource_queue_id < 0) {
        fprintf(stderr, "Failed to create resource manager message queue\n");
        signal_handler(SIGTERM);
        return 1;
    }
    
    // Khởi tạo danh sách message queue IDs cho các tab
    tab_queue_ids = (int *)malloc(sizeof(int) * max_tabs);
    if (tab_queue_ids == NULL) {
        fprintf(stderr, "Failed to allocate memory for tab queue IDs\n");
        signal_handler(SIGTERM);
        return 1;
    }
    
    for (int i = 0; i < max_tabs; i++) {
        tab_queue_ids[i] = -1; // Chưa được tạo
    }
    
    // Khởi tạo semaphore set
    semid = create_semaphore();
    if (semid < 0) {
        fprintf(stderr, "Failed to create semaphore set\n");
        signal_handler(SIGTERM);
        return 1;
    }

    printf("[Browser] Started. Message queue ID: %d, Semaphore ID: %d\n",
           browser_queue_id, semid);

    while (1) {
        // Kiểm tra FIFO để tương thích ngược
        fd = open(BROWSER_FIFO, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            while (read(fd, &msg, sizeof(msg)) > 0) {
                printf("[Browser] Legacy FIFO: Tab %d sent: %s (type %d)\n", 
                       msg.tab_id, msg.command, msg.type);
                handle_command(&msg);
            }
            close(fd);
        }
        
        // Đọc từ message queue
        if (receive_message(browser_queue_id, &msg_data, 0, sizeof(BrowserMessage)) > 0) {
            memcpy(&msg, &msg_data.message, sizeof(BrowserMessage));
            printf("[Browser] Message Queue: Tab %d sent: %s (type %d)\n", 
                   msg.tab_id, msg.command, msg.type);
            
            // Lock semaphore trước khi xử lý lệnh
            lock_semaphore(semid, SEM_BROWSER);
            
            handle_command(&msg);
            
            // Unlock semaphore sau khi xử lý xong
            unlock_semaphore(semid, SEM_BROWSER);
        }
        
        // Sleep một chút để tránh tiêu tốn CPU
        usleep(10000); // 10ms
    }

    return 0;
}

