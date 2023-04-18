#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

// clang-format off

static const char basic_sensor[] = "{\n"
	"\"sensor_id\":1,\n"
	"\"timeout\":100000000000000000000000,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		"{\"name\": \"load_5\", \"system\":  \"echo 3\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"load_15\", \"system\": \"echo 2\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"no_unit\", \"system\": \"echo 5\","
					"\"send\": 1},\n"
	"]\n"
	"}";

static const char monitor_send_parameter_sensor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"// This line will NOT be sent\n"
		"{\"name\": \"load_1\", \"system\":  \"echo 1\","
					"\"unit\": \"%\", \"send\": 0},"
		"// This line will be sent too: the default behavior is"
		"// send:1\n"
		"{\"name\": \"load_5\", \"system\": \"echo 3\","
							"\"unit\": \"%\"},"
		"// This line will be sent: \"send:1\"\n"
		"{\"name\": \"load_15\", \"system\": \"echo 2\","
					"\"unit\": \"%\", \"send\": 1},"

		"// This line will also be sent\n"
		"{\"name\": \"no_unit\", \"system\": \"echo 5\","
					"\"send\": 1},\n"
	"]"
	"}";

static const char monitor_integer[] = "{"
	"\"sensor_id\":1,\n"
	"\"timeout\":100000000000000000000000,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		// Force integer: is an actual integer
		"{\"name\": \"forced_int\", \"system\": \"echo 5\","
			"\"unit\": \"%\", \"send\": 1, \"integer\":1},\n"
		// Force integer: is not an actual integer
		"{\"name\": \"bad_int\", \"system\": \"echo text\","
			"\"unit\": \"%\", \"send\": 1, \"integer\":1},\n"
	"]\n"
	"}";

#define TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)                            \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	mvalue_type("value",mvalue,                                            \
	CHILD_S("type","system",NULL)))))

#define TEST1_CHECKS0(mmonitor,mvalue_type,mvalue)                             \
	CHILD_S("unit","%",                                                    \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue))

#define TEST1_CHECKS0_NOUNIT0(mmonitor,mvalue_type,mvalue)                     \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)

#define TEST1_CHECKS0_NOUNIT(mmonitor,mvalue) \
			TEST1_CHECKS0_NOUNIT0(mmonitor,CHILD_S,mvalue)

#define TEST1_CHECKS(mmonitor,mvalue) TEST1_CHECKS0(mmonitor,CHILD_S,mvalue)

static void prepare_test_basic_sensor_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		JSON_KEY_TEST(TEST1_CHECKS("load_5","3.000000")),
		JSON_KEY_TEST(TEST1_CHECKS("load_15","2.000000")),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));

	json_key_test checks_nu[] = {
		JSON_KEY_TEST(TEST1_CHECKS0_NOUNIT("no_unit","5.000000")),
	};

	check_list_push_checks(check_list, checks_nu, RD_ARRAYSIZE(checks_nu));
}

/** Basic test */
TEST_FN(test_basic_sensor, prepare_test_basic_sensor_checks, basic_sensor)

/** Test monitor line send parameter */
TEST_FN(test_monitor_send_parameter, prepare_test_basic_sensor_checks,
						monitor_send_parameter_sensor)

/** Test force integer parameter */
static void prepare_test_integer_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		JSON_KEY_TEST(TEST1_CHECKS0("forced_int",CHILD_I,5)),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));
}

TEST_FN(test_monitor_integer, prepare_test_integer_checks, monitor_integer)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_basic_sensor),
		cmocka_unit_test(test_monitor_send_parameter),
		cmocka_unit_test(test_monitor_integer),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
