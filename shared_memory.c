#include "shared_memory.h"

// Tạo hoặc kết nối vào shared memory
void *create_shared_memory() {
    int shmid;
    void *shared_memory = NULL;
    
    // Tạo shared memory segment
    shmid = shmget(SHM_KEY, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid < 0) {
        perror("shmget failed");
        return NULL;
    }
    
    // Attach shared memory segment
    shared_memory = shmat(shmid, NULL, 0);
    if (shared_memory == (void *) -1) {
        perror("shmat failed");
        return NULL;
    }
    
    printf("Shared memory attached at %p\n", shared_memory);
    return shared_memory;
}

// Ngắt kết nối và xóa shared memory
void destroy_shared_memory(void *shared_memory, int shmid) {
    // Detach shared memory
    if (shmdt(shared_memory) < 0) {
        perror("shmdt failed");
    }
    
    // Xóa shared memory segment
    if (shmctl(shmid, IPC_RMID, NULL) < 0) {
        perror("shmctl failed");
    }
}

// Lưu dữ liệu vào shared memory
int store_data_in_shared_memory(SharedMemorySegment *shm, int tab_id, const char *url, const char *data, int data_size) {
    // Kiểm tra kích thước dữ liệu
    if (data_size > MAX_HTML_SIZE) {
        printf("Data too large for shared memory segment\n");
        return -1;
    }
    
    // Thiết lập thông tin
    shm->is_used = 1;
    shm->tab_id = tab_id;
    shm->data_size = data_size;
    strncpy(shm->url, url, sizeof(shm->url) - 1);
    shm->url[sizeof(shm->url) - 1] = '\0';
    
    // Sao chép dữ liệu
    memcpy(shm->data, data, data_size);
    shm->data[data_size] = '\0'; // Đảm bảo null-terminated
    
    return 0;
}

// Đọc dữ liệu từ shared memory
int read_data_from_shared_memory(SharedMemorySegment *shm, int tab_id, char *data, int *data_size) {
    // Kiểm tra xem segment có được sử dụng không
    if (!shm->is_used) {
        printf("Shared memory segment not in use\n");
        return -1;
    }
    
    // Kiểm tra tab_id có khớp không
    if (shm->tab_id != tab_id) {
        printf("Tab ID mismatch\n");
        return -2;
    }
    
    // Sao chép dữ liệu
    *data_size = shm->data_size;
    memcpy(data, shm->data, shm->data_size);
    data[shm->data_size] = '\0'; // Đảm bảo null-terminated
    
    return 0;
} 