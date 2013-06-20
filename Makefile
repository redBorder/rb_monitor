PROGNAME=rb_monitor

all: $(PROGNAME) 

CC = cc
CFLAGS = -g -W -Wall -Wno-missing-field-initializers 

PREFIX=/opt/rb

clean: 
	-rm -rf main

rb_monitor: main.c
	$(CC) $(CFLAGS) -o $@ $< -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -std=gnu99

install:
	install -t $(PREFIX)/bin $(PROGNAME)
 
