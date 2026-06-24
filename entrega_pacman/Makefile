CC = gcc
CFLAGS = -std=c11 -Wall -Wextra -Werror -Iinclude

TARGET = pacman
SRCS = src/main.c src/client.c src/server.c src/network.c src/protocol.c src/transmission.c src/files.c src/game.c src/log.c
OBJS = src/*.o

DIST_NAME = grr20243784_grr2021
DIST_FILE = $(DIST_NAME).tgz

.PHONY: all clean dist

all: $(TARGET)

$(TARGET): $(SRCS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

dist: clean all
	rm -rf /tmp/$(DIST_NAME)
	mkdir -p /tmp/$(DIST_NAME)
	cp -r Makefile README.md relatorio.pdf include src maps pastilhas $(TARGET) /tmp/$(DIST_NAME)/
	tar -czf $(DIST_FILE) -C /tmp $(DIST_NAME)
	rm -rf /tmp/$(DIST_NAME)
	@echo "Arquivo gerado: $(DIST_FILE)"

clean:
	rm -f $(TARGET) $(OBJS)
