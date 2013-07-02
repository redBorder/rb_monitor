PROGNAME=rb_monitor

all: $(PROGNAME) 

CC = cc
CFLAGS = -g -W -Wall -Wno-missing-field-initializers -DNDEBUG

PREFIX=/opt/rb

clean: 
	-rm -rf $(PROGNAME)

$(PROGNAME): main.c
	$(CC) $(CFLAGS) -o $@ $< -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -lmatheval -std=gnu99

install:
	install -t $(PREFIX)/bin $(PROGNAME)
 
