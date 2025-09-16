CC := gcc
XFLAGS := -Wall -std=c11
CFLAGS := $(XFLAGS) $(shell pkg-config --libs --cflags libevdev)

.PHONY: build clean

build: $(wildcard *.c)
	$(CC) $(CFLAGS) $^ -o mouse-autoscroll