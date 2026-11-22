CC = gcc
CFLAGS = -O3 -Wall -std=c99
LDFLAGS ?=
PREFIX ?= /usr/local
DESTDIR ?=

ifeq ($(OS),Windows_NT)
	TARGET = beer.exe
	BUILD_RULE = windows
	RUN_CMD = .\$(TARGET)
	RM_CMD = del /Q $(TARGET) 2>nul || exit 0
else
	TARGET = beer
	BUILD_RULE = posix
	RUN_CMD = ./$(TARGET)
	RM_CMD = rm -f $(TARGET)
endif

all: $(BUILD_RULE)

windows:
	$(CC) $(CFLAGS) beer.c -o beer.exe $(LDFLAGS) -luser32 -lwinmm

posix:
	$(CC) $(CFLAGS) beer.c -o beer $(LDFLAGS) -lm

install: posix
	install -d "$(DESTDIR)$(PREFIX)/bin"
	install -m 755 beer "$(DESTDIR)$(PREFIX)/bin/beer"

uninstall:
	rm -f "$(DESTDIR)$(PREFIX)/bin/beer"

run: all
	$(RUN_CMD)

clean:
	$(RM_CMD)

.PHONY: all windows posix install uninstall run clean
