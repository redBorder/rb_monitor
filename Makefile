
BIN = rb_monitor

SRCS = main.c rb_snmp.c rb_value.c rb_values_list.c rb_zk.c rb_monitor_zk.c
OBJS = $(SRCS:.c=.o)
TESTS = $(patsubst %.c,%.test,$(wildcard tests/0*.c))
TESTS_OBJS = $(wildcard tests/*.o)
TESTS_XML = $(wildcard tests/*.xml)
COV_FILES = $(shell find . -type f -name '*.gcda' -o -name '*.gcno')

.PHONY: version.c tests coverage check_coverage

all: $(BIN)

include mklove/Makefile.base

version.c:
	@rm -f $@
	@echo "const char *nprobe_revision=\"`git describe --abbrev=6 --tags HEAD --always`\";" >> $@
	@echo "const char *version=\"6.13.`date +"%y%m%d"`\";" >> $@

clean: bin-clean
	rm -f $(TESTS) $(TESTS_OBJS) $(TESTS_XML) $(COV_FILES)

install: bin-install

tests_compile: $(TESTS)

tests: tests_compile
	cd tests; ./run_tests.sh

tests/%.xml: tests

tests/%.test: CPPFLAGS := -I. $(CPPFLAGS)
tests/%.test: tests/%.o $(filter-out main.o,$(OBJS)) tests/json_test.o main.c
	$(CC) $(CPPFLAGS) $(LDFLAGS) $(filter-out main.c,$^) -o $@ $(LIBS) -lcmocka

check_coverage:
	@( if [[ "x$(WITH_COVERAGE)" == "xn" ]]; then \
	echo "$(MKL_RED) You need to configure using --enable-coverage"; \
	echo -n "$(MKL_CLR_RESET)"; \
	false; \
	fi)

COVERAGE_INFO ?= coverage.info
COVERAGE_OUTPUT_DIRECTORY ?= coverage.out.html
COV_VALGRIND ?= valgrind
COV_GCOV ?= gcov
COV_LCOV ?= lcov

coverage: check_coverage tests_compile
	( for test in $(TESTS); do ./$$test; done )
	$(COV_LCOV) --gcov-tool=$(COV_GCOV) -q \
                --rc lcov_branch_coverage=1 --capture \
                --directory ./ --output-file ${COVERAGE_INFO}
	genhtml --branch-coverage ${COVERAGE_INFO} --output-directory \
				${COVERAGE_OUTPUT_DIRECTORY} > coverage.out
	# ./display_coverage.sh

-include $(DEPS)
