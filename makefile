CC = gcc
CFLAGS = -Wall -g

all: browser tab testUI renderer resource_manager

browser: browser.c common.h shared_memory.c shared_memory.h
	$(CC) $(CFLAGS) -o browser browser.c shared_memory.c

tab: tab.c common.h shared_memory.c shared_memory.h
	$(CC) $(CFLAGS) -o tab tab.c shared_memory.c -lncurses -pthread

testUI: testUI.c
	$(CC) $(CFLAGS) -o testUI testUI.c -lncurses

renderer: renderer.c common.h shared_memory.c shared_memory.h
	$(CC) $(CFLAGS) -o renderer renderer.c shared_memory.c

resource_manager: resource_manager.c common.h shared_memory.c shared_memory.h
	$(CC) $(CFLAGS) -o resource_manager resource_manager.c shared_memory.c

clean:
	rm -f browser tab testUI renderer resource_manager

