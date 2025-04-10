#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <pthread.h>
#include <ncurses.h>
#include <signal.h>
#include "common.h"
#include "shared_memory.h"
#include "message_queue.h"
#include "semaphore.h"

int tab_id;
int write_fd;
char response_fifo[64];
WINDOW *mainwin;
pthread_mutex_t display_mutex = PTHREAD_MUTEX_INITIALIZER;

// Shared memory
SharedMemorySegment *shared_mem = NULL;
void *shm_pointer = NULL;
int shmid = -1;

// Message queue
int browser_queue_id = -1;
int tab_queue_id = -1;

// Semaphore
int semid = -1;

// Cấu trúc lưu lịch sử duyệt web
#define MAX_HISTORY 10
typedef struct {
    char urls[MAX_HISTORY][256];
    int current;
    int count;
} BrowsingHistory;

BrowsingHistory history = {{{0}}, -1, 0};

// Thêm URL vào lịch sử
void add_to_history(const char *url) {
    if (history.current < MAX_HISTORY - 1) {
        // Xóa các mục phía trước nếu đang ở giữa lịch sử
        history.count = history.current + 1;
        
        // Thêm URL mới vào cuối
        history.current++;
        strncpy(history.urls[history.current], url, 255);
        history.urls[history.current][255] = '\0';
        history.count++;
    } else {
        // Di chuyển lịch sử lên
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            strcpy(history.urls[i], history.urls[i + 1]);
        }
        strncpy(history.urls[MAX_HISTORY - 1], url, 255);
        history.urls[MAX_HISTORY - 1][255] = '\0';
        history.current = MAX_HISTORY - 1;
    }
}

// Di chuyển lịch sử lùi
const char *go_back() {
    if (history.current > 0) {
        history.current--;
        return history.urls[history.current];
    }
    return NULL;
}

// Di chuyển lịch sử tới
const char *go_forward() {
    if (history.current < history.count - 1) {
        history.current++;
        return history.urls[history.current];
    }
    return NULL;
}

// Xử lý tín hiệu để dọn dẹp
void signal_handler(int sig) {
    printf("Tab %d received signal %d, cleaning up...\n", tab_id, sig);
    endwin();
    
    // Ngắt kết nối shared memory
    if (shm_pointer != NULL) {
        shmdt(shm_pointer);
    }
    
    // Hủy legacy FIFO
    unlink(response_fifo);
    
    // Hủy message queue của tab
    if (tab_queue_id >= 0) {
        destroy_message_queue(tab_queue_id);
    }
    
    exit(0);
}

// Hiển thị nội dung trên giao diện
void update_display(const char *content, int is_error) {
    pthread_mutex_lock(&display_mutex);
    
    werase(mainwin);
    box(mainwin, 0, 0);
    mvwprintw(mainwin, 1, 2, "Mini Browser - Tab %d", tab_id);
    
    if (history.current >= 0) {
        mvwprintw(mainwin, 2, 2, "URL: %s", history.urls[history.current]);
    } else {
        mvwprintw(mainwin, 2, 2, "URL: [None]");
    }
    
    // Hiển thị nội dung với các dòng được cắt ngắn nếu cần
    int line = 4;
    char buffer[MAX_MSG];
    strncpy(buffer, content, MAX_MSG - 1);
    buffer[MAX_MSG - 1] = '\0';
    
    char *token = strtok(buffer, "\n");
    while (token && line < 10) {
        if (is_error) {
            wattron(mainwin, A_BOLD);
            mvwprintw(mainwin, line++, 4, "%s", token);
            wattroff(mainwin, A_BOLD);
        } else {
            mvwprintw(mainwin, line++, 4, "%s", token);
        }
        token = strtok(NULL, "\n");
    }
    
    mvwprintw(mainwin, 11, 2, "Command > ");
    wrefresh(mainwin);
    move(11, 13);
    
    pthread_mutex_unlock(&display_mutex);
}

// Gửi thông điệp đến browser thông qua message queue
void send_to_browser(BrowserMessage *msg) {
    MessageQueueData msg_data;
    msg_data.mtype = 1; // Loại mặc định
    memcpy(&msg_data.message, msg, sizeof(BrowserMessage));
    
    // Thử gửi qua message queue
    if (browser_queue_id >= 0) {
        if (send_message(browser_queue_id, &msg_data, sizeof(BrowserMessage)) >= 0) {
            printf("[Tab %d] Sent message to browser via message queue\n", tab_id);
            return;
        }
        // Nếu gửi thất bại, thử gửi qua FIFO
    }
    
    // Sử dụng legacy FIFO nếu message queue không hoạt động
    if (write_fd >= 0) {
        if (write(write_fd, msg, sizeof(BrowserMessage)) >= 0) {
            printf("[Tab %d] Sent message to browser via legacy FIFO\n", tab_id);
        } else {
            perror("write to browser fifo");
        }
    } else {
        fprintf(stderr, "[Tab %d] Failed to send message to browser\n", tab_id);
    }
}

// Thread lắng nghe phản hồi
void *listen_response(void *arg) {
    MessageQueueData msg_data;
    BrowserMessage response;
    int legacy_fd = -1;
    
    while (1) {
        int received = 0;
        
        // Thử nhận từ message queue
        if (tab_queue_id >= 0) {
            if (receive_message(tab_queue_id, &msg_data, 0, sizeof(BrowserMessage)) > 0) {
                memcpy(&response, &msg_data.message, sizeof(BrowserMessage));
                received = 1;
                printf("[Tab %d] Received response via message queue\n", tab_id);
            }
        }
        
        // Nếu không nhận được từ message queue, thử FIFO
        if (!received) {
            if (legacy_fd < 0) {
                legacy_fd = open(response_fifo, O_RDONLY | O_NONBLOCK);
            }
            
            if (legacy_fd >= 0) {
                int bytes = read(legacy_fd, &response, sizeof(response));
                if (bytes > 0) {
                    received = 1;
                    printf("[Tab %d] Received response via legacy FIFO\n", tab_id);
                } else if (bytes < 0 && errno != EAGAIN) {
                    close(legacy_fd);
                    legacy_fd = -1;
                }
            }
        }
        
        if (received) {
            printf("[Tab %d] Received response type %d\n", tab_id, response.type);
            
            int is_error = (response.type == MSG_ERROR);
            
            if (response.has_shared_data) {
                // Lock semaphore trước khi đọc shared memory
                if (semid >= 0) {
                    lock_semaphore(semid, SEM_SHARED_MEM);
                }
                
                // Đọc dữ liệu từ shared memory
                char content[MAX_HTML_SIZE];
                int content_size;
                
                int result = read_data_from_shared_memory(shared_mem, tab_id, content, &content_size);
                
                // Unlock semaphore sau khi đọc xong
                if (semid >= 0) {
                    unlock_semaphore(semid, SEM_SHARED_MEM);
                }
                
                if (result == 0) {
                    // Hiển thị nội dung từ shared memory
                    update_display(content, is_error);
                } else {
                    update_display("Error: Failed to read from shared memory", 1);
                }
            } else {
                // Hiển thị thông báo từ command
                update_display(response.command, is_error);
                
                // Xử lý các loại phản hồi đặc biệt
                if (response.type == MSG_PAGE_LOADED) {
                    // Chỉ cần đợi phản hồi từ renderer
                }
            }
        }
        
        // Sleep một chút để tránh tiêu tốn CPU
        usleep(10000); // 10ms
    }

    if (legacy_fd >= 0) {
        close(legacy_fd);
    }
    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <tab_id>\n", argv[0]);
        return 1;
    }

    // Đăng ký handler xử lý tín hiệu
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    tab_id = atoi(argv[1]);
    snprintf(response_fifo, sizeof(response_fifo), "%s%d", RESPONSE_FIFO_PREFIX, tab_id);
    mkfifo(response_fifo, 0666);

    // Kết nối vào browser (FIFO legacy)
    write_fd = open(BROWSER_FIFO, O_WRONLY);
    if (write_fd < 0) {
        perror("open browser fifo");
        // Không thoát, sẽ thử message queue
    }

    // Khởi tạo message queue cho tab
    key_t tab_key = get_tab_queue_key(tab_id);
    tab_queue_id = create_message_queue(tab_key);
    if (tab_queue_id < 0) {
        fprintf(stderr, "Failed to create tab message queue\n");
        // Không thoát, sẽ thử FIFO
    } else {
        printf("[Tab %d] Message queue created with ID: %d\n", tab_id, tab_queue_id);
    }
    
    // Kết nối vào message queue của browser
    browser_queue_id = msgget(BROWSER_QUEUE_KEY, 0);
    if (browser_queue_id < 0) {
        perror("msgget browser queue");
        // Không thoát, sẽ thử FIFO
    } else {
        printf("[Tab %d] Connected to browser message queue with ID: %d\n", tab_id, browser_queue_id);
    }
    
    // Kết nối vào semaphore set
    semid = semget(SEM_KEY, 0, 0);
    if (semid < 0) {
        perror("semget");
        // Không thoát, sẽ tiếp tục nhưng không dùng semaphore
    } else {
        printf("[Tab %d] Connected to semaphore set with ID: %d\n", tab_id, semid);
    }

    // Kết nối vào shared memory
    shm_pointer = shmat(shmget(SHM_KEY, 0, 0), NULL, 0);
    if (shm_pointer == (void *)-1) {
        perror("shmat");
        // Không thoát, sẽ tiếp tục nhưng không dùng shared memory
    } else {
        shared_mem = (SharedMemorySegment *)shm_pointer;
        printf("[Tab %d] Connected to shared memory\n", tab_id);
    }

    BrowserMessage msg;
    msg.tab_id = tab_id;

    // Khởi tạo ncurses
    initscr();
    cbreak();
    echo();
    keypad(stdscr, TRUE);
    curs_set(1);

    int height = 15, width = 80, starty = 1, startx = 1;
    mainwin = newwin(height, width, starty, startx);
    box(mainwin, 0, 0);
    mvwprintw(mainwin, 1, 2, "Mini Browser - Tab %d", tab_id);
    mvwprintw(mainwin, 2, 2, "Using: %s", (browser_queue_id >= 0) ? "Message Queue" : "Legacy FIFO");
    mvwprintw(mainwin, 11, 2, "Command > ");
    wrefresh(mainwin);
    move(11, 13);

    // Khởi động thread lắng nghe phản hồi
    pthread_t listener;
    pthread_create(&listener, NULL, listen_response, NULL);

    char input[MAX_MSG];
    while (1) {
        mvwgetnstr(mainwin, 11, 13, input, MAX_MSG);
        if (strcmp(input, "exit") == 0) break;
        
        // Xử lý lệnh đặc biệt
        if (strcmp(input, "back") == 0) {
            // Quay lại trang trước
            const char *prev_url = go_back();
            if (prev_url) {
                // Gửi lệnh load trang trước
                char load_cmd[MAX_MSG];
                snprintf(load_cmd, sizeof(load_cmd), "load %s", prev_url);
                strncpy(msg.command, load_cmd, MAX_MSG);
            } else {
                strncpy(msg.command, "back", MAX_MSG);
            }
            msg.type = MSG_BACK;
        }
        else if (strcmp(input, "forward") == 0) {
            // Đi đến trang tiếp theo
            const char *next_url = go_forward();
            if (next_url) {
                // Gửi lệnh load trang tiếp theo
                char load_cmd[MAX_MSG];
                snprintf(load_cmd, sizeof(load_cmd), "load %s", next_url);
                strncpy(msg.command, load_cmd, MAX_MSG);
            } else {
                strncpy(msg.command, "forward", MAX_MSG);
            }
            msg.type = MSG_FORWARD;
        }
        else if (strncmp(input, "load ", 5) == 0) {
            // Tải trang mới, thêm vào lịch sử
            add_to_history(input + 5);
            strncpy(msg.command, input, MAX_MSG);
            msg.type = MSG_LOAD_PAGE;
        }
        else if (strcmp(input, "reload") == 0) {
            strncpy(msg.command, input, MAX_MSG);
            msg.type = MSG_RELOAD;
        }
        else {
            strncpy(msg.command, input, MAX_MSG);
            msg.type = MSG_ERROR; // Default type
        }
        
        msg.has_shared_data = 0; // Tab không gửi dữ liệu qua shared memory
        send_to_browser(&msg);

        mvwprintw(mainwin, 11, 13, "%*s", MAX_MSG, "");
        move(11, 13);
        wrefresh(mainwin);
    }

    endwin();
    
    // Đóng các kết nối
    if (write_fd >= 0) {
        close(write_fd);
    }
    
    if (shm_pointer != NULL) {
        shmdt(shm_pointer);
    }
    
    if (tab_queue_id >= 0) {
        destroy_message_queue(tab_queue_id);
    }
    
    unlink(response_fifo);
    return 0;
}

