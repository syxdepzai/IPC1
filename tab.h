#ifndef TAB_H
#define TAB_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <ncurses.h>
#include <pthread.h>

#include "common.h"
#include "shared_memory.h"
#include "message_queue.h"
#include "semaphore.h"

#define BROWSER_FIFO "/tmp/browser_fifo"
#define RESPONSE_FIFO_PREFIX "/tmp/response_fifo_"

// History structure
typedef struct {
    char urls[100][512];
    int count;
    int current;
} BrowserHistory;

// Các khai báo hàm
void signal_handler(int sig);
void update_display(const char* message, int is_error);
void send_to_browser(BrowserMessage* msg);
const char* go_back();
const char* go_forward();
void open_link_by_number(int link_number);
void process_command(const char* command);

#endif /* TAB_H */ 