#ifndef SHARED_MEMORY_H
#define SHARED_MEMORY_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#define SHM_KEY 0x1234       // Khóa cho shared memory
#define SHM_SIZE 4096*10     // Kích thước 40KB cho shared memory
#define MAX_HTML_SIZE 32768  // Kích thước tối đa cho nội dung HTML

// Định nghĩa cấu trúc cho shared memory
typedef struct {
    int is_used;             // Flag để biết phân đoạn này đang được sử dụng không
    int tab_id;              // ID của tab sở hữu phân đoạn này
    int data_size;           // Kích thước dữ liệu thực tế
    char url[256];           // URL được tải
    char data[MAX_HTML_SIZE]; // Dữ liệu HTML
} SharedMemorySegment;

// Hàm để tạo hoặc kết nối vào shared memory
void *create_shared_memory();

// Hàm để ngắt kết nối và xóa shared memory
void destroy_shared_memory(void *shared_memory, int shmid);

// Hàm để lưu dữ liệu vào shared memory
int store_data_in_shared_memory(SharedMemorySegment *shm, int tab_id, const char *url, const char *data, int data_size);

// Hàm để đọc dữ liệu từ shared memory
int read_data_from_shared_memory(SharedMemorySegment *shm, int tab_id, char *data, int *data_size);

#endif 