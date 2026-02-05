# Control Center for Linux - C/GTK4 Implementation
# Makefile

CC = gcc
CFLAGS = -Wall -O2 $(shell pkg-config --cflags gtk4)
LDFLAGS = $(shell pkg-config --libs gtk4) -lm
TARGET = controlcenter
SRC_DIR = src
OBJ_DIR = obj

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/keyboard.c \
          $(SRC_DIR)/system.c \
          $(SRC_DIR)/ui.c \
          $(SRC_DIR)/gauges.c \
          $(SRC_DIR)/colorwheel.c

OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))

all: $(OBJ_DIR) $(TARGET) kb_ctl kb_gui

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	$(CC) $(CFLAGS) -c $< -o $@

# Standalone CLI tool (no GTK dependency)
kb_ctl: $(SRC_DIR)/kb_ctl.c
	$(CC) -Wall -O2 -o $@ $<

# Standalone GTK4 GUI with hotkey support
kb_gui: $(SRC_DIR)/kb_gui.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lpthread

# Background service daemon
kb_service: $(SRC_DIR)/kb_service.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -rf $(OBJ_DIR) $(TARGET) kb_ctl kb_gui kb_service

install: $(TARGET) kb_ctl kb_gui kb_service
	install -m 755 $(TARGET) /usr/local/bin/
	install -m 755 kb_ctl /usr/local/bin/
	install -m 755 kb_gui /usr/local/bin/
	install -m 755 kb_service /usr/local/bin/
	install -m 644 controlcenter.desktop /usr/share/applications/

.PHONY: all clean install

