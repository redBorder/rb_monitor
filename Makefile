PROGNAME=rb_monitor

all: $(PROGNAME) 

CC ?= cc
CFLAGS ?= -g -W -Wall -Wno-missing-field-initializers -DWITH_LIBRD -DNDEBUG -O3

PREFIX?=/opt/rb
LIBRDKAFKA_INCLUDES ?= /opt/rb/include
LIBRD_INCLUDES ?= /opt/rb/include
LIBRDKAFKA_LIBRARIES ?= /opt/rb/lib
LIBRD_LIBRARIES ?= /opt/rb/lib

CFLAGS+= -I${LIBRDKAFKA_INCLUDES} -I${LIBRD_INCLUDES}
LDFLAGS+= -L${LIBRDKAFKA_LIBRARIES} -L${LIBRD_LIBRARIES}

#0.7 supp
#CFLAGS+= -DKAFKA_07

clean: 
	-rm -rf $(PROGNAME)

$(PROGNAME): main.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -ljson -lpthread -lrd -lrt -lz -lsnmp -lrdkafka -lmatheval -std=gnu99

install:
	install -t $(PREFIX)/bin $(PROGNAME)
 
