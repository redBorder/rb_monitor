all: main

CC = cc
CFLAGS = -g -W -Wall -Wno-missing-field-initializers 

clean: 
	-rm -rf main

main: main.c
	$(CC) $(CFLAGS) -o $@ $< -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -std=gnu99 
