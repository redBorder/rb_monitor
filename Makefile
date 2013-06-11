all: main

CC = cc
CFLAGS = -g

main: main.c
	$(CC) $(CFLAGS) -o $@ $< -ljson
