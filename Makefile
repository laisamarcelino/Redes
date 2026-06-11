CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Iinclude
TARGET = pacman
SRCS = src/main.c src/client.c src/server.c src/network.c src/protocol.c src/transmission.c src/files.c src/game.c
OBJS = src/*.o
TEST = $(word 2,$(MAKECMDGOALS))
TEST_FILE = tests/$(TEST)
TEST_BIN = /tmp/$(basename $(TEST))
TEST_SRCS = $(filter-out src/main.c,$(SRCS))

.PHONY: all clean test

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

test:
ifeq ($(TEST),)
	$(error Use: make test nome_do_teste.c)
endif
	$(CC) $(CFLAGS) $(TEST_FILE) $(TEST_SRCS) -o $(TEST_BIN)
	$(TEST_BIN)

%.c:
	@:

clean:
	rm -f $(TARGET) $(OBJS)
