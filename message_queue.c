#include "message_queue.h"

// Khởi tạo message queue
int create_message_queue(key_t key) {
    int msqid = msgget(key, IPC_CREAT | 0666);
    if (msqid < 0) {
        perror("msgget failed");
        return -1;
    }
    return msqid;
}

// Hủy message queue
int destroy_message_queue(int msqid) {
    int result = msgctl(msqid, IPC_RMID, NULL);
    if (result < 0) {
        perror("msgctl failed");
        return -1;
    }
    return 0;
}

// Gửi thông điệp
int send_message(int msqid, MessageQueueData *data, size_t size) {
    int result = msgsnd(msqid, data, size, 0);
    if (result < 0) {
        perror("msgsnd failed");
        return -1;
    }
    return 0;
}

// Nhận thông điệp
int receive_message(int msqid, MessageQueueData *data, long msgtype, size_t size) {
    ssize_t result = msgrcv(msqid, data, size, msgtype, 0);
    if (result < 0) {
        if (errno != EINTR) {  // Bỏ qua lỗi do bị ngắt bởi signal
            perror("msgrcv failed");
        }
        return -1;
    }
    return result;
}

// Tạo key cho tab theo ID
key_t get_tab_queue_key(int tab_id) {
    return TAB_QUEUE_KEY_BASE + tab_id;
} 