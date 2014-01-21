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

OBJECTS=rb_snmp.o rb_value.o rb_values_list.o rb_log.o main.o

.PHONY: clean tests install

clean: 
	-rm -rf $(PROGNAME) $(OBJECTS)

rb_log.o:rb_log.c rb_log.h
	$(CC) $(CFLAGS) -o $@ $< -c

rb_snmp.o:rb_snmp.c rb_snmp.h
	$(CC) $(CFLAGS) -o $@ $< -c

main.o:main.c
	$(CC) $(CFLAGS) -o $@ $< -c -std=gnu99

rb_value.o:rb_value.c rb_value.h
	$(CC) $(CFLAGS) -o $@ $< -c 

rb_values_list.o:rb_values_list.c rb_values_list.h
	$(CC) $(CFLAGS) -o $@ $< -c 

$(PROGNAME): $(OBJECTS) rb_libmatheval.h rb_system.h 
	$(CC) $(CFLAGS) -o $@ $(OBJECTS) $(LDFLAGS) -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -lmatheval 

install:
	install -t $(PREFIX)/bin $(PROGNAME)

TESTS = tests/test01.json tests/test02.json tests/test03.json tests/test04.json \
        tests/test05.json tests/test06.json tests/test07.json tests/test08.json \
        tests/test09.json tests/test10.json 


test: $(PROGNAME)
	for test in ${TESTS}; do \
	  sh test.sh ${PROGNAME} $$test; \
        done
