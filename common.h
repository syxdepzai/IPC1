#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>

#define MAX_MSG 512
#define BROWSER_FIFO "/tmp/browser_fifo"
#define RESPONSE_FIFO_PREFIX "/tmp/tab_response_"

// Định nghĩa cho tiến trình mới
#define RENDERER_FIFO "/tmp/renderer_fifo"
#define RESOURCE_FIFO "/tmp/resource_fifo"

// Các loại thông điệp
typedef enum {
    MSG_LOAD_PAGE,       // Yêu cầu tải trang
    MSG_PAGE_LOADED,     // Trang đã được tải
    MSG_RENDER_CONTENT,  // Yêu cầu render nội dung
    MSG_CONTENT_RENDERED,// Nội dung đã được render
    MSG_BACK,            // Quay lại trang trước
    MSG_FORWARD,         // Đi đến trang tiếp theo
    MSG_RELOAD,          // Tải lại trang hiện tại
    MSG_ERROR,           // Báo lỗi
    MSG_CACHE_REQUEST,   // Yêu cầu cache
    MSG_CACHE_RESPONSE   // Phản hồi từ cache
} MessageType;

// Cấu trúc thông điệp cơ bản
typedef struct {
    int tab_id;
    MessageType type;
    char command[MAX_MSG];
    int shared_memory_offset;  // Offset vào shared memory nếu dữ liệu lớn
    int has_shared_data;       // Flag để biết có dữ liệu trong shared memory không
} BrowserMessage;

#endif

