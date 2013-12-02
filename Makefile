PROGNAME=rb_monitor

all: $(PROGNAME) 

CC ?= cc
CFLAGS ?= -g -W -Wall -Wno-missing-field-initializers -DWITH_LIBRD #-DNDEBUG -Os

PREFIX?=/opt/rb
LIBRDKAFKA_INCLUDES ?= /opt/rb/include
LIBRD_INCLUDES ?= /opt/rb/include
LIBRDKAFKA_LIBRARIES ?= /opt/rb/lib
LIBRD_LIBRARIES ?= /opt/rb/lib

CFLAGS+= -I${LIBRDKAFKA_INCLUDES} -I${LIBRD_INCLUDES}
LDFLAGS+= -L${LIBRDKAFKA_LIBRARIES} -L${LIBRD_LIBRARIES}

OBJECTS=rb_snmp.o rb_values_list.o rb_log.o main.o

clean: 
	-rm -rf $(PROGNAME) $(OBJECTS)

rb_log.o:rb_log.c rb_log.h
	$(CC) $(CFLAGS) -o $@ $< -c

rb_snmp.o:rb_snmp.c rb_snmp.h
	$(CC) $(CFLAGS) -o $@ $< -c

main.o:main.c
	$(CC) $(CFLAGS) -o $@ $< -c -std=gnu99

rb_values_list.o:rb_values_list.c rb_values_list.h
	$(CC) $(CFLAGS) -o $@ $< -c 

$(PROGNAME): $(OBJECTS) rb_libmatheval.h rb_system.h 
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS) -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -lmatheval 

install:
	install -t $(PREFIX)/bin $(PROGNAME)
 
