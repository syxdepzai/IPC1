#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

// Định nghĩa union semun nếu chưa được định nghĩa trong hệ thống
#if defined(__GNU_LIBRARY__) && !defined(_SEM_SEMUN_UNDEFINED)
// Union được định nghĩa bởi include <sys/sem.h>
#else
union semun {
    int val;                // Giá trị cho SETVAL
    struct semid_ds *buf;   // Buffer cho IPC_STAT, IPC_SET
    unsigned short *array;  // Array cho GETALL, SETALL
    struct seminfo *__buf;  // Buffer cho IPC_INFO (Linux-specific)
};
#endif

// Khoá cho semaphore set
#define SEM_KEY 0x4321

// Các index của semaphore trong set
#define SEM_SHARED_MEM 0    // Semaphore cho shared memory
#define SEM_BROWSER 1       // Semaphore cho browser
#define SEM_RENDERER 2      // Semaphore cho renderer
#define SEM_RESOURCE 3      // Semaphore cho resource manager
#define SEM_COUNT 4         // Tổng số semaphores

// Định nghĩa các hoạt động
#define SEM_LOCK -1
#define SEM_UNLOCK 1

// Hàm khởi tạo semaphore set
int create_semaphore();

// Hàm hủy semaphore set
int destroy_semaphore(int semid);

// Hàm thực hiện hoạt động V (tăng) hoặc P (giảm) trên semaphore
int semaphore_operation(int semid, int sem_num, int op);

// Hàm lock một semaphore (P operation)
int lock_semaphore(int semid, int sem_num);

// Hàm unlock một semaphore (V operation)
int unlock_semaphore(int semid, int sem_num);

#endif 