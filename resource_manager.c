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

#define MAX_CACHE_ENTRIES 10
#define CACHE_DIR "/tmp/browser_cache/"

// Cấu trúc lưu thông tin cache
typedef struct {
    char url[256];
    char cache_file[256];
    time_t timestamp;
    int is_valid;
} CacheEntry;

// Biến toàn cục
CacheEntry cache[MAX_CACHE_ENTRIES];
SharedMemorySegment *shared_mem = NULL;
void *shm_pointer = NULL;
int shmid = -1;

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

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("[Resource Manager] Received signal %d, cleaning up...\n", sig);
    if (shm_pointer != NULL) {
        destroy_shared_memory(shm_pointer, shmid);
    }
    unlink(RESOURCE_FIFO);
    exit(0);
}

int main() {
    int fd;
    BrowserMessage msg, response;
    
    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // Khởi tạo cache
    init_cache();
    
    // Tạo FIFO cho resource manager
    mkfifo(RESOURCE_FIFO, 0666);
    printf("[Resource Manager] Started. Listening on %s...\n", RESOURCE_FIFO);
    
    // Kết nối vào shared memory
    shm_pointer = create_shared_memory();
    if (shm_pointer == NULL) {
        fprintf(stderr, "Failed to create shared memory\n");
        return 1;
    }
    shared_mem = (SharedMemorySegment *)shm_pointer;
    
    while (1) {
        // Mở FIFO để đọc
        fd = open(RESOURCE_FIFO, O_RDONLY);
        if (fd < 0) {
            perror("open resource fifo");
            continue;
        }
        
        // Đọc thông điệp
        while (read(fd, &msg, sizeof(msg)) > 0) {
            printf("[Resource Manager] Received message type %d from tab %d\n", msg.type, msg.tab_id);
            
            // Xử lý các loại thông điệp
            if (msg.type == MSG_CACHE_REQUEST) {
                // Tìm trong cache
                char url[256];
                strncpy(url, msg.command, sizeof(url));
                int cache_index = find_in_cache(url);
                
                response.tab_id = msg.tab_id;
                response.type = MSG_CACHE_RESPONSE;
                response.has_shared_data = 0;
                
                if (cache_index >= 0) {
                    // Tìm thấy trong cache
                    char cache_content[MAX_HTML_SIZE];
                    int content_size;
                    
                    if (read_from_cache(cache_index, cache_content, &content_size, MAX_HTML_SIZE) == 0) {
                        // Lưu vào shared memory
                        if (store_data_in_shared_memory(shared_mem, msg.tab_id, url, cache_content, content_size) == 0) {
                            response.has_shared_data = 1;
                            strcpy(response.command, "CACHE_HIT");
                        } else {
                            strcpy(response.command, "ERROR: Failed to store in shared memory");
                        }
                    } else {
                        strcpy(response.command, "ERROR: Failed to read from cache");
                    }
                } else {
                    // Không tìm thấy trong cache
                    strcpy(response.command, "CACHE_MISS");
                }
                
                // Gửi phản hồi
                char response_fifo[64];
                snprintf(response_fifo, sizeof(response_fifo), "%s%d", RESPONSE_FIFO_PREFIX, msg.tab_id);
                int resp_fd = open(response_fifo, O_WRONLY);
                if (resp_fd >= 0) {
                    write(resp_fd, &response, sizeof(response));
                    close(resp_fd);
                }
            } else if (msg.type == MSG_LOAD_PAGE) {
                // Trang đã được tải, lưu vào cache
                if (msg.has_shared_data) {
                    char content[MAX_HTML_SIZE];
                    int content_size;
                    
                    if (read_data_from_shared_memory(shared_mem, msg.tab_id, content, &content_size) == 0) {
                        // Thêm vào cache
                        add_to_cache(msg.command, content, content_size);
                    }
                }
            }
        }
        
        close(fd);
    }
    
    return 0;
} 