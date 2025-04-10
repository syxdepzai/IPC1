#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include "common.h"
#include "shared_memory.h"
#include "message_queue.h"
#include "semaphore.h"

#define MAX_CACHE_ENTRIES 10
#define CACHE_DIR "/tmp/browser_cache/"

// Cấu trúc lưu thông tin cache
typedef struct {
    char url[256];
    char cache_file[256];
    time_t timestamp;
    int is_valid;
} CacheEntry;

// Khai báo prototype của các hàm
void process_cache_request(BrowserMessage *msg, BrowserMessage *response);
void process_load_page(BrowserMessage *msg);

// Biến toàn cục
CacheEntry cache[MAX_CACHE_ENTRIES];
SharedMemorySegment *shared_mem = NULL;
void *shm_pointer = NULL;
int shmid = -1;

// Biến toàn cục cho message queue
int resource_queue_id = -1;
int *tab_queue_ids = NULL;
int max_tabs = 10;

// Biến toàn cục cho semaphore
int semid = -1;

// Khởi tạo hệ thống cache
void init_cache() {
    // Tạo thư mục cache nếu chưa tồn tại
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "mkdir -p %s", CACHE_DIR);
    system(cmd);
    
    // Khởi tạo các entry cache
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        cache[i].is_valid = 0;
    }
    
    printf("[Resource Manager] Cache initialized at %s\n", CACHE_DIR);
}

// Tìm URL trong cache
int find_in_cache(const char *url) {
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (cache[i].is_valid && strcmp(cache[i].url, url) == 0) {
            return i;
        }
    }
    return -1;
}

// Thêm mục vào cache
int add_to_cache(const char *url, const char *content, int content_size) {
    // Tìm chỗ trống trong cache
    int slot = -1;
    time_t oldest_time = time(NULL);
    int oldest_slot = 0;
    
    for (int i = 0; i < MAX_CACHE_ENTRIES; i++) {
        if (!cache[i].is_valid) {
            slot = i;
            break;
        }
        if (cache[i].timestamp < oldest_time) {
            oldest_time = cache[i].timestamp;
            oldest_slot = i;
        }
    }
    
    // Nếu không có chỗ trống, sử dụng mục cũ nhất
    if (slot == -1) {
        slot = oldest_slot;
    }
    
    // Tạo tên file cache
    snprintf(cache[slot].url, sizeof(cache[slot].url), "%s", url);
    snprintf(cache[slot].cache_file, sizeof(cache[slot].cache_file), 
             "%s%d_%ld.html", CACHE_DIR, slot, time(NULL));
    cache[slot].timestamp = time(NULL);
    cache[slot].is_valid = 1;
    
    // Lưu nội dung vào file cache
    FILE *fp = fopen(cache[slot].cache_file, "w");
    if (!fp) {
        printf("[Resource Manager] Failed to create cache file\n");
        return -1;
    }
    
    fwrite(content, 1, content_size, fp);
    fclose(fp);
    
    printf("[Resource Manager] Cached: %s -> %s\n", url, cache[slot].cache_file);
    return slot;
}

// Đọc nội dung từ cache
int read_from_cache(int cache_index, char *content, int *content_size, int max_size) {
    if (cache_index < 0 || cache_index >= MAX_CACHE_ENTRIES || !cache[cache_index].is_valid) {
        printf("[Resource Manager] Invalid cache index\n");
        return -1;
    }
    
    FILE *fp = fopen(cache[cache_index].cache_file, "r");
    if (!fp) {
        printf("[Resource Manager] Failed to open cache file\n");
        cache[cache_index].is_valid = 0; // Đánh dấu là không hợp lệ
        return -1;
    }
    
    // Lấy kích thước file
    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    if (file_size > max_size) {
        printf("[Resource Manager] Cache content too large for buffer\n");
        fclose(fp);
        return -1;
    }
    
    // Đọc nội dung
    *content_size = fread(content, 1, file_size, fp);
    content[*content_size] = '\0';
    fclose(fp);
    
    // Cập nhật timestamp
    cache[cache_index].timestamp = time(NULL);
    
    return 0;
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
                printf("[Resource Manager] Sent response to Tab %d via legacy FIFO\n", tab_id);
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
            printf("[Resource Manager] Sent response to Tab %d via legacy FIFO\n", tab_id);
        } else {
            perror("open response fifo");
        }
    } else {
        printf("[Resource Manager] Sent response to Tab %d via message queue\n", tab_id);
    }
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("[Resource Manager] Received signal %d, cleaning up...\n", sig);
    
    // Hủy shared memory
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    
    // Hủy FIFO
    unlink(RESOURCE_FIFO);
    
    // Hủy message queue
    if (resource_queue_id >= 0) {
        destroy_message_queue(resource_queue_id);
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
    
    // Khởi tạo cache
    init_cache();
    
    // Tạo FIFO cho resource manager (legacy, để tương thích ngược)
    mkfifo(RESOURCE_FIFO, 0666);
    printf("[Resource Manager] Legacy FIFO created at %s...\n", RESOURCE_FIFO);
    
    // Khởi tạo message queue
    resource_queue_id = create_message_queue(RESOURCE_QUEUE_KEY);
    if (resource_queue_id < 0) {
        fprintf(stderr, "Failed to create resource message queue\n");
        return 1;
    }
    
    // Kết nối vào semaphore set
    semid = semget(SEM_KEY, 0, 0);
    if (semid < 0) {
        perror("semget");
        // Vẫn tiếp tục, nhưng không sử dụng semaphore
    } else {
        printf("[Resource Manager] Connected to semaphore set with ID: %d\n", semid);
    }
    
    // Kết nối vào shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;
    
    printf("[Resource Manager] Started. Message queue ID: %d\n", resource_queue_id);
    
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
        fd = open(RESOURCE_FIFO, O_RDONLY | O_NONBLOCK);
        if (fd >= 0) {
            while (read(fd, &msg, sizeof(msg)) > 0) {
                printf("[Resource Manager] Legacy FIFO: Received message type %d from tab %d\n", 
                       msg.type, msg.tab_id);
                
                // Xử lý thông điệp
                if (semid >= 0) {
                    lock_semaphore(semid, SEM_RESOURCE);
                }
                
                // Xử lý các loại thông điệp
                if (msg.type == MSG_CACHE_REQUEST) {
                    process_cache_request(&msg, &response);
                    send_response_to_tab(&response);
                } else if (msg.type == MSG_LOAD_PAGE) {
                    process_load_page(&msg);
                }
                
                if (semid >= 0) {
                    unlock_semaphore(semid, SEM_RESOURCE);
                }
                
                received = 1;
            }
            close(fd);
        }
        
        // Đọc từ message queue
        if (receive_message(resource_queue_id, &msg_data, 0, sizeof(BrowserMessage)) > 0) {
            memcpy(&msg, &msg_data.message, sizeof(BrowserMessage));
            printf("[Resource Manager] Message Queue: Received message type %d from tab %d\n", 
                   msg.type, msg.tab_id);
                   
            // Xử lý thông điệp
            if (semid >= 0) {
                lock_semaphore(semid, SEM_RESOURCE);
            }
            
            // Xử lý các loại thông điệp
            if (msg.type == MSG_CACHE_REQUEST) {
                process_cache_request(&msg, &response);
                send_response_to_tab(&response);
            } else if (msg.type == MSG_LOAD_PAGE) {
                process_load_page(&msg);
            }
            
            if (semid >= 0) {
                unlock_semaphore(semid, SEM_RESOURCE);
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

// Xử lý yêu cầu cache
void process_cache_request(BrowserMessage *msg, BrowserMessage *response) {
    // Tìm trong cache
    char url[256];
    strncpy(url, msg->command, sizeof(url));
    int cache_index = find_in_cache(url);
    
    response->tab_id = msg->tab_id;
    response->type = MSG_CACHE_RESPONSE;
    response->has_shared_data = 0;
    
    if (cache_index >= 0) {
        // Tìm thấy trong cache
        char cache_content[MAX_HTML_SIZE];
        int content_size;
        
        if (read_from_cache(cache_index, cache_content, &content_size, MAX_HTML_SIZE) == 0) {
            // Lock semaphore trước khi truy cập shared memory
            if (semid >= 0) {
                lock_semaphore(semid, SEM_SHARED_MEM);
            }
            
            // Lưu vào shared memory
            if (store_data_in_shared_memory(shared_mem, msg->tab_id, url, cache_content, content_size) == 0) {
                response->has_shared_data = 1;
                strcpy(response->command, "CACHE_HIT");
            } else {
                strcpy(response->command, "ERROR: Failed to store in shared memory");
            }
            
            // Unlock shared memory
            if (semid >= 0) {
                unlock_semaphore(semid, SEM_SHARED_MEM);
            }
        } else {
            strcpy(response->command, "ERROR: Failed to read from cache");
        }
    } else {
        // Không tìm thấy trong cache
        strcpy(response->command, "CACHE_MISS");
    }
}

// Xử lý trang đã được tải
void process_load_page(BrowserMessage *msg) {
    // Trang đã được tải, lưu vào cache
    if (msg->has_shared_data) {
        // Lock semaphore trước khi đọc shared memory
        if (semid >= 0) {
            lock_semaphore(semid, SEM_SHARED_MEM);
        }
        
        char content[MAX_HTML_SIZE];
        int content_size;
        
        if (read_data_from_shared_memory(shared_mem, msg->tab_id, content, &content_size) == 0) {
            // Thêm vào cache
            add_to_cache(msg->command, content, content_size);
        }
        
        // Unlock shared memory
        if (semid >= 0) {
            unlock_semaphore(semid, SEM_SHARED_MEM);
        }
    }
} 