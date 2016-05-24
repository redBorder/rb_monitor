#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char zero_sensor[] = "{\n"
	"\"sensor_id\":1,\n"
	"\"timeout\":100000000000000000000000,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		"{\"name\": \"z\", \"system\":  \"echo 0\", \"nonzero\":1,"
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"nz\", \"system\": \"echo 1\", \"nonzero\":1,"
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"zop\", \"op\": \"nz-nz\", \"nonzero\":1,"
					" \"unit\": \"%\", \"send\": 1},\n"
#if 0
// @TODO this should work, but it doesn't
		"{\"name\": \"nzop\", \"op\": \"nz+nz\", \"nonzero\":1,"
					" \"unit\": \"%\", \"send\": 1},\n"
#endif
		// Trying to use a bad marked value
		"{\"name\": \"op_with_z\", \"op\": \"z+3\", \"nonzero\":1,"
					" \"unit\": \"%\", \"send\": 1},\n"
	"]\n"
	"}";

#define TEST1_CHECKS0(mmonitor,mtype,mvalue_type,mvalue)                       \
	(struct json_key_test[]) {                                             \
		CHILD_I("sensor_id",1),                                        \
		CHILD_S("sensor_name","sensor-arriba"),                        \
		CHILD_S("monitor",mmonitor),                                   \
		mvalue_type("value",mvalue),                                   \
		CHILD_S("type",mtype),                                         \
		CHILD_S("unit","%"),                                           \
	}

#define TEST1_CHECKS(mtype,mmonitor,mvalue) \
	TEST1_CHECKS0(mmonitor,mtype,CHILD_S,mvalue)

#define TEST1_SAMPLE TEST1_CHECKS("t","a","b")
#define TEST1_SIZE sizeof(TEST1_SAMPLE)/sizeof(TEST1_SAMPLE[0])

/// @TODO prepare empty_check
static void prepare_zero_sensor_checks(check_list_t *check_list) {
	struct json_key_test *checks[] = {
		// TEST1_CHECKS("z","0.000000"), // No zero!
		TEST1_CHECKS("system","nz","1.000000"),
		// TEST1_CHECKS("op","zop","0.000000"), // No zero!
		// @TODO this should work, but it doesn't
		// TEST1_CHECKS("op","nzop","2.000000"),
		// TEST1_CHECKS("op","nzop","2.000000"),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks),
								TEST1_SIZE);
}

/** Basic test */
TEST_FN(test_zero_sensor, prepare_zero_sensor_checks, zero_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_zero_sensor),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
