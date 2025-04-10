#include "semaphore.h"

// Khởi tạo semaphore set
int create_semaphore() {
    int semid;
    union semun arg;
    unsigned short values[SEM_COUNT];
    
    // Tạo semaphore set với SEM_COUNT semaphores
    semid = semget(SEM_KEY, SEM_COUNT, IPC_CREAT | 0666);
    if (semid < 0) {
        perror("semget failed");
        return -1;
    }
    
    // Khởi tạo tất cả semaphores là 1 (không bị lock)
    for (int i = 0; i < SEM_COUNT; i++) {
        values[i] = 1;
    }
    
    arg.array = values;
    if (semctl(semid, 0, SETALL, arg) < 0) {
        perror("semctl SETALL failed");
        semctl(semid, 0, IPC_RMID, 0);
        return -1;
    }
    
    printf("Semaphore set created with ID: %d\n", semid);
    return semid;
}

// Hủy semaphore set
int destroy_semaphore(int semid) {
    if (semctl(semid, 0, IPC_RMID, 0) < 0) {
        perror("semctl IPC_RMID failed");
        return -1;
    }
    return 0;
}

// Thực hiện hoạt động P (giảm) hoặc V (tăng) trên semaphore
int semaphore_operation(int semid, int sem_num, int op) {
    struct sembuf sb;
    
    sb.sem_num = sem_num;
    sb.sem_op = op;
    sb.sem_flg = 0; // Tác vụ chờ nếu không thực hiện được
    
    if (semop(semid, &sb, 1) < 0) {
        if (errno != EINTR) { // Bỏ qua lỗi do bị ngắt bởi signal
            perror("semop failed");
        }
        return -1;
    }
    
    return 0;
}

// Lock một semaphore (P operation)
int lock_semaphore(int semid, int sem_num) {
    return semaphore_operation(semid, sem_num, SEM_LOCK);
}

// Unlock một semaphore (V operation)
int unlock_semaphore(int semid, int sem_num) {
    return semaphore_operation(semid, sem_num, SEM_UNLOCK);
} 