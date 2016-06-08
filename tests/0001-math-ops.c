#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char ops_monitor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\":  \"echo 3\","
							"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo 2\","
							"\"unit\": \"%\"},"
		"// Operation\n"
		"{\"name\": \"100load_5\", \"op\":\"100*load_5\","
							"\"unit\": \"%\"},"
		"// 2 variables operation\n"
		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
							"\"unit\": \"%\"},"

	"]"
	"}";

#define TEST1_CHECKS(mmonitor,mvalue,mtype) (struct json_key_test[]) {         \
	CHILD_I("sensor_id",1),                                                \
	CHILD_S("sensor_name","sensor-arriba"),                                \
	CHILD_S("monitor",mmonitor),                                           \
	CHILD_S("value",mvalue),                                               \
	CHILD_S("type",mtype),                                                 \
	CHILD_S("unit","%"),                                                   \
}

#define TEST1_SAMPLE TEST1_CHECKS("a","b","c")
#define TEST1_SIZE sizeof(TEST1_SAMPLE)/sizeof(TEST1_SAMPLE[0])

static void prepare_math_ops_checks(check_list_t *check_list) {
	struct json_key_test *checks[] = {
		TEST1_CHECKS("load_1","3.000000","system"),
		TEST1_CHECKS("load_5","2.000000","system"),
		TEST1_CHECKS("100load_5","200.000000","op"),
		TEST1_CHECKS("load_5_x_load_1","6.000000","op"),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks),
								TEST1_SIZE);
}

/** Basic test */
TEST_FN(test_ops, prepare_math_ops_checks, ops_monitor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ops),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
