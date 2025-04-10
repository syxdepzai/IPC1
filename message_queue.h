#ifndef MESSAGE_QUEUE_H
#define MESSAGE_QUEUE_H

#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "common.h"

// Các khóa cho message queues
#define BROWSER_QUEUE_KEY 0x2345
#define RENDERER_QUEUE_KEY 0x2346
#define RESOURCE_QUEUE_KEY 0x2347
#define TAB_QUEUE_KEY_BASE 0x3000 // Tab IDs được thêm vào Base

// Định nghĩa cấu trúc message
typedef struct {
    long mtype;             // Loại thông điệp (>0)
    BrowserMessage message; // Nội dung thông điệp (từ common.h)
} MessageQueueData;

// Hàm khởi tạo message queue
int create_message_queue(key_t key);

// Hàm hủy message queue
int destroy_message_queue(int msqid);

// Hàm gửi thông điệp
int send_message(int msqid, MessageQueueData *data, size_t size);

// Hàm nhận thông điệp
int receive_message(int msqid, MessageQueueData *data, long msgtype, size_t size);

// Hàm tạo key cho tab theo ID
key_t get_tab_queue_key(int tab_id);

#endif 