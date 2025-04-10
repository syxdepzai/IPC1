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
int shmid = -1;
void *shm_pointer = NULL;

// Hàm render HTML thông qua w3m
void render_html(const char *html_content, int content_size, char *output, int output_size) {
    // Lưu nội dung HTML vào file tạm
    FILE *temp_file = fopen("/tmp/render_temp.html", "w");
    if (!temp_file) {
        snprintf(output, output_size, "Error: Cannot create temporary file");
        return;
    }
    fwrite(html_content, 1, content_size, temp_file);
    fclose(temp_file);
    
    // Sử dụng w3m để render HTML
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "w3m -dump /tmp/render_temp.html > /tmp/rendered.txt");
    system(cmd);
    
    // Đọc kết quả render
    FILE *fp = fopen("/tmp/rendered.txt", "r");
    if (!fp) {
        snprintf(output, output_size, "Error: Cannot open rendered output");
        return;
    }
    
    // Đọc nội dung file đã render
    output[0] = '\0';
    char line[256];
    while (fgets(line, sizeof(line), fp) && strlen(output) + strlen(line) < output_size - 1) {
        strcat(output, line);
    }
    fclose(fp);
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("Renderer received signal %d, cleaning up...\n", sig);
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    unlink(RENDERER_FIFO);
    exit(0);
}

int main() {
    int fd;
    BrowserMessage msg, response;
    
    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Tạo FIFO cho renderer
    mkfifo(RENDERER_FIFO, 0666);
    printf("[Renderer] Started. Listening on %s...\n", RENDERER_FIFO);
    
    // Kết nối vào shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;
    
    while (1) {
        // Mở FIFO để đọc
        fd = open(RENDERER_FIFO, O_RDONLY);
        if (fd < 0) {
            perror("open renderer fifo");
            continue;
        }
        
        // Đọc thông điệp
        while (read(fd, &msg, sizeof(msg)) > 0) {
            printf("[Renderer] Received message type %d from tab %d\n", msg.type, msg.tab_id);
            
            // Xử lý các loại thông điệp
            if (msg.type == MSG_RENDER_CONTENT) {
                if (msg.has_shared_data) {
                    // Đọc nội dung từ shared memory
                    char html_content[MAX_HTML_SIZE];
                    int content_size;
                    
                    if (read_data_from_shared_memory(shared_mem, msg.tab_id, html_content, &content_size) == 0) {
                        // Render nội dung HTML
                        char rendered_output[MAX_MSG];
                        render_html(html_content, content_size, rendered_output, MAX_MSG);
                        
                        // Chuẩn bị phản hồi
                        response.tab_id = msg.tab_id;
                        response.type = MSG_CONTENT_RENDERED;
                        strncpy(response.command, rendered_output, MAX_MSG - 1);
                        response.command[MAX_MSG - 1] = '\0';
                        
                        // Gửi phản hồi thông qua fifo
                        char response_fifo[64];
                        snprintf(response_fifo, sizeof(response_fifo), "%s%d", RESPONSE_FIFO_PREFIX, msg.tab_id);
                        int resp_fd = open(response_fifo, O_WRONLY);
                        if (resp_fd >= 0) {
                            write(resp_fd, &response, sizeof(response));
                            close(resp_fd);
                        }
                    } else {
                        printf("[Renderer] Failed to read data from shared memory\n");
                    }
                } else {
                    printf("[Renderer] No shared data available\n");
                }
            } else {
                printf("[Renderer] Unsupported message type\n");
            }
        }
        
        close(fd);
    }
    
    return 0;
} 