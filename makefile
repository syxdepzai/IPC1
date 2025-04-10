CC = gcc
CFLAGS = -Wall -g -I.
LDFLAGS = 

# Kiểm tra hệ điều hành để xử lý các tùy chọn biên dịch
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    LDLIBS_IPC = -lrt
else
    LDLIBS_IPC =
endif

# Thư viện chung
LDLIBS_COMMON = $(LDLIBS_IPC)
LDLIBS_UI = -lncurses
LDLIBS_THREAD = -pthread

# Thành phần IPC
IPC_SRCS = shared_memory.c message_queue.c semaphore.c
IPC_OBJS = $(IPC_SRCS:.c=.o)
IPC_DEPS = shared_memory.h message_queue.h semaphore.h common.h

.PHONY: all clean

all: browser tab renderer resource_manager testUI

# Quy tắc cho các file đối tượng
%.o: %.c $(IPC_DEPS)
	$(CC) $(CFLAGS) -c $< -o $@

# Các thành phần chính
browser: browser.c $(IPC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS_COMMON)

tab: tab.c $(IPC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS_COMMON) $(LDLIBS_UI) $(LDLIBS_THREAD)

renderer: renderer.c $(IPC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS_COMMON)

resource_manager: resource_manager.c $(IPC_OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS_COMMON)

# Xử lý riêng cho testUI để tránh phụ thuộc vào GTK nếu không cần
testUI: testUI.c
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS_UI) || echo "Warning: testUI build failed, skipping..."

# Dọn dẹp
clean:
	rm -f *.o browser tab renderer resource_manager testUI
	-ipcrm -a 2>/dev/null || echo "Note: ipcrm command failed or not available"

# Thông tin
info:
	@echo "Makefile for IPC Browser Project"
	@echo "Available targets:"
	@echo "  make all      - Build all components"
	@echo "  make browser  - Build the browser component"
	@echo "  make tab      - Build the tab component" 
	@echo "  make renderer - Build the renderer component"
	@echo "  make resource_manager - Build the resource manager"
	@echo "  make testUI   - Build the test UI (optional)"
	@echo "  make clean    - Remove all built files"
	@echo "  make info     - Show this help message"

