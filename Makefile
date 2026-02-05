# BackLit - Keyboard Backlight Control
# Makefile

CC = gcc

# Default target builds all tools
all: kb_gui kb_ctl kb_service

# Standalone CLI tool (no dependencies)
kb_ctl: src/kb_ctl.c
	$(CC) -Wall -O2 -o $@ $<

# Background service daemon (no dependencies)
kb_service: src/kb_service.c
	$(CC) -Wall -O2 -o $@ $<

# GTK4 GUI application (requires GTK4)
kb_gui: src/kb_gui.c
	$(CC) -Wall -O2 $$(pkg-config --cflags gtk4) -o $@ $< $$(pkg-config --libs gtk4) -lm -lpthread

clean:
	rm -f kb_ctl kb_gui kb_service

install: kb_gui kb_ctl kb_service
	install -m 755 kb_gui /usr/local/bin/
	install -m 755 kb_ctl /usr/local/bin/
	install -m 755 kb_service /usr/local/bin/
	install -m 644 controlcenter.desktop /usr/share/applications/

.PHONY: all clean install
