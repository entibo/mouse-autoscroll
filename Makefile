CC := gcc
XFLAGS := -Wall -std=c11 -lm
CFLAGS := $(XFLAGS) $(shell pkg-config --libs --cflags libevdev dbus-1)

.PHONY: build clean

build: $(wildcard *.c)
	$(CC) $(CFLAGS) $^ -o mouse-autoscroll