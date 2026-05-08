CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror
TARGET = pacman
SRCS = src/main.c src/client.c src/server.c src/network.c src/util.c

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)

