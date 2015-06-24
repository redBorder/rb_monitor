
include Makefile.config

BIN = rb_monitor

SRCS = main.c rb_log.c rb_snmp.c rb_value.c rb_values_list.c rb_zk.c rb_monitor_zk.c
OBJS = $(SRCS:.c=.o)

.PHONY: version.c tests

all: $(BIN)

include mklove/Makefile.base

version.c: 
	@rm -f $@
	@echo "const char *nprobe_revision=\"`git describe --abbrev=6 --tags HEAD --always`\";" >> $@
	@echo "const char *version=\"6.13.`date +"%y%m%d"`\";" >> $@

clean: bin-clean

install: bin-install

TESTS = tests/test01.json tests/test02.json tests/test03.json tests/test04.json \
        tests/test05.json tests/test06.json tests/test07.json tests/test08.json \
        tests/test09.json tests/test10.json tests/test11.json tests/test12.json

test: $(PROGNAME)
	for test in ${TESTS}; do \
	  sh test.sh ${PROGNAME} $$test; \
        done

-include $(DEPS)
