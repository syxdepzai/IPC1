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
int shmid = -1;
void *shm_pointer = NULL;

// Biến toàn cục cho message queue
int renderer_queue_id = -1;
int *tab_queue_ids = NULL;
int max_tabs = 10;

// Biến toàn cục cho semaphore
int semid = -1;

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

// Gửi phản hồi đến tab
void send_response_to_tab(BrowserMessage *response) {
    int tab_id = response->tab_id;
    
    // Kiểm tra xem tab_queue_ids đã được khởi tạo chưa
    if (tab_queue_ids == NULL) {
        tab_queue_ids = (int *)malloc(sizeof(int) * max_tabs);
        if (tab_queue_ids == NULL) {
            fprintf(stderr, "Failed to allocate memory for tab queue IDs\n");
            return;
        }
        
        for (int i = 0; i < max_tabs; i++) {
            tab_queue_ids[i] = -1;
        }
    }
    
    // Tìm hoặc tạo message queue cho tab
    if (tab_id >= max_tabs || tab_queue_ids[tab_id] < 0) {
        key_t key = get_tab_queue_key(tab_id);
        tab_queue_ids[tab_id] = msgget(key, 0);
        if (tab_queue_ids[tab_id] < 0) {
            fprintf(stderr, "Failed to find message queue for Tab %d\n", tab_id);
            
            // Thử gửi thông qua FIFO (legacy)
            char response_fifo[64];
            snprintf(response_fifo, sizeof(response_fifo), "%s%d", RESPONSE_FIFO_PREFIX, tab_id);
            int fd = open(response_fifo, O_WRONLY);
            if (fd >= 0) {
                write(fd, response, sizeof(BrowserMessage));
                close(fd);
                printf("[Renderer] Sent response to Tab %d via legacy FIFO\n", tab_id);
            } else {
                perror("open response fifo");
            }
            return;
        }
    }
    
    // Tạo message để gửi
    MessageQueueData msg_data;
    msg_data.mtype = 1; // Loại mặc định
    memcpy(&msg_data.message, response, sizeof(BrowserMessage));
    
    // Gửi message
    if (send_message(tab_queue_ids[tab_id], &msg_data, sizeof(BrowserMessage)) < 0) {
        fprintf(stderr, "Failed to send message to Tab %d\n", tab_id);
        
        // Thử gửi thông qua FIFO (legacy)
        char response_fifo[64];
        snprintf(response_fifo, sizeof(response_fifo), "%s%d", RESPONSE_FIFO_PREFIX, tab_id);
        int fd = open(response_fifo, O_WRONLY);
        if (fd >= 0) {
            write(fd, response, sizeof(BrowserMessage));
            close(fd);
            printf("[Renderer] Sent response to Tab %d via legacy FIFO\n", tab_id);
        } else {
            perror("open response fifo");
        }
    } else {
        printf("[Renderer] Sent response to Tab %d via message queue\n", tab_id);
    }
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("Renderer received signal %d, cleaning up...\n", sig);
    
    // Hủy shared memory
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    
    // Hủy FIFO
    unlink(RENDERER_FIFO);
    
    // Hủy message queue
    if (renderer_queue_id >= 0) {
        destroy_message_queue(renderer_queue_id);
    }
    
    // Hủy tab queue IDs
    if (tab_queue_ids) {
        free(tab_queue_ids);
    }
    
    exit(0);
}

int main() {
    int fd;
    BrowserMessage msg, response;
    MessageQueueData msg_data;
    
    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Tạo FIFO cho renderer (legacy, để tương thích ngược)
    mkfifo(RENDERER_FIFO, 0666);
    printf("[Renderer] Legacy FIFO created at %s...\n", RENDERER_FIFO);
    
    // Khởi tạo message queue
    renderer_queue_id = create_message_queue(RENDERER_QUEUE_KEY);
    if (renderer_queue_id < 0) {
        fprintf(stderr, "Failed to create renderer message queue\n");
        return 1;
    }
    
    // Kết nối vào semaphore set
    semid = semget(SEM_KEY, 0, 0);
    if (semid < 0) {
        perror("semget");
        // Vẫn tiếp tục, nhưng không sử dụng semaphore
    } else {
        printf("[Renderer] Connected to semaphore set with ID: %d\n", semid);
    }
    
    // Kết nối vào shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;
    
    printf("[Renderer] Started. Message queue ID: %d\n", renderer_queue_id);
    
    // Khởi tạo tab_queue_ids
    tab_queue_ids = (int *)malloc(sizeof(int) * max_tabs);
    if (tab_queue_ids == NULL) {
        fprintf(stderr, "Failed to allocate memory for tab queue IDs\n");
        signal_handler(SIGTERM);
        return 1;
    }
    
    for (int i = 0; i < max_tabs; i++) {
        tab_queue_ids[i] = -1;
    }
    
    while (1) {
        int received = 0;
        
        // Kiểm tra FIFO để tương thích ngược
        fd = open(RENDERER_FIFO, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            while (read(fd, &msg, sizeof(msg)) > 0) {
                printf("[Renderer] Legacy FIFO: Received message type %d from tab %d\n", 
                       msg.type, msg.tab_id);
                
                // Xử lý thông điệp
                if (semid >= 0) {
                    lock_semaphore(semid, SEM_RENDERER);
                }
                
                if (msg.type == MSG_RENDER_CONTENT) {
                    if (msg.has_shared_data) {
                        // Lock shared memory trước khi đọc
                        if (semid >= 0) {
                            lock_semaphore(semid, SEM_SHARED_MEM);
                        }
                        
                        // Đọc nội dung từ shared memory
                        char html_content[MAX_HTML_SIZE];
                        int content_size;
                        
                        if (read_data_from_shared_memory(shared_mem, msg.tab_id, html_content, &content_size) == 0) {
                            // Unlock shared memory sau khi đọc xong
                            if (semid >= 0) {
                                unlock_semaphore(semid, SEM_SHARED_MEM);
                            }
                            
                            // Render nội dung HTML
                            char rendered_output[MAX_MSG];
                            render_html(html_content, content_size, rendered_output, MAX_MSG);
                            
                            // Chuẩn bị phản hồi
                            response.tab_id = msg.tab_id;
                            response.type = MSG_CONTENT_RENDERED;
                            strncpy(response.command, rendered_output, MAX_MSG - 1);
                            response.command[MAX_MSG - 1] = '\0';
                            response.has_shared_data = 0;
                            
                            // Gửi phản hồi
                            send_response_to_tab(&response);
                        } else {
                            printf("[Renderer] Failed to read data from shared memory\n");
                            
                            // Unlock shared memory nếu đọc thất bại
                            if (semid >= 0) {
                                unlock_semaphore(semid, SEM_SHARED_MEM);
                            }
                        }
                    } else {
                        printf("[Renderer] No shared data available\n");
                    }
                } else {
                    printf("[Renderer] Unsupported message type\n");
                }
                
                if (semid >= 0) {
                    unlock_semaphore(semid, SEM_RENDERER);
                }
                
                received = 1;
            }
            close(fd);
        }
        
        // Đọc từ message queue
        if (receive_message(renderer_queue_id, &msg_data, 0, sizeof(BrowserMessage)) > 0) {
            memcpy(&msg, &msg_data.message, sizeof(BrowserMessage));
            printf("[Renderer] Message Queue: Received message type %d from tab %d\n", 
                   msg.type, msg.tab_id);
                   
            // Xử lý thông điệp
            if (semid >= 0) {
                lock_semaphore(semid, SEM_RENDERER);
            }
            
            if (msg.type == MSG_RENDER_CONTENT) {
                if (msg.has_shared_data) {
                    // Lock shared memory trước khi đọc
                    if (semid >= 0) {
                        lock_semaphore(semid, SEM_SHARED_MEM);
                    }
                    
                    // Đọc nội dung từ shared memory
                    char html_content[MAX_HTML_SIZE];
                    int content_size;
                    
                    if (read_data_from_shared_memory(shared_mem, msg.tab_id, html_content, &content_size) == 0) {
                        // Unlock shared memory sau khi đọc xong
                        if (semid >= 0) {
                            unlock_semaphore(semid, SEM_SHARED_MEM);
                        }
                        
                        // Render nội dung HTML
                        char rendered_output[MAX_MSG];
                        render_html(html_content, content_size, rendered_output, MAX_MSG);
                        
                        // Chuẩn bị phản hồi
                        response.tab_id = msg.tab_id;
                        response.type = MSG_CONTENT_RENDERED;
                        strncpy(response.command, rendered_output, MAX_MSG - 1);
                        response.command[MAX_MSG - 1] = '\0';
                        response.has_shared_data = 0;
                        
                        // Gửi phản hồi
                        send_response_to_tab(&response);
                    } else {
                        printf("[Renderer] Failed to read data from shared memory\n");
                        
                        // Unlock shared memory nếu đọc thất bại
                        if (semid >= 0) {
                            unlock_semaphore(semid, SEM_SHARED_MEM);
                        }
                    }
                } else {
                    printf("[Renderer] No shared data available\n");
                }
            } else {
                printf("[Renderer] Unsupported message type\n");
            }
            
            if (semid >= 0) {
                unlock_semaphore(semid, SEM_RENDERER);
            }
            
            received = 1;
        }
        
        // Nếu không có message nào, sleep một chút để giảm CPU usage
        if (!received) {
            usleep(10000); // 10ms
        }
    }
    
    return 0;
} 