CC = gcc
CFLAGS = -O3 -Wall -std=c99

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
	$(CC) $(CFLAGS) beer.c -o beer.exe -luser32 -lwinmm

posix:
	$(CC) $(CFLAGS) beer.c -o beer -lm

run:
	$(RUN_CMD)

clean:
	$(RM_CMD)

.PHONY: all windows posix run clean
