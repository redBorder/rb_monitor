#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char invalid_monitor[] = "{\n"
	"\"sensor_id\":1,\n"
	"\"timeout\":100000000000000000000000,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		// Monitor with no name
		"{\"system\":  \"echo 3\", \"send\": 1},\n"

		// Unknown monitor type (no system, oid, op)
		"{\"name\": \"unknown_op\","
					" \"unit\": \"%\", \"send\": 1},\n"

		// Empty returned value
		"{\"name\": \"null\", \"system\": \"echo -n\","
					" \"unit\": \"%\", \"send\": 1},\n"

		// Unknown monitor key
		"{\"name\": \"invalid_key\", \"system\":  \"echo 12\","
			" \"unit\": \"%\", \"send\": 1, \"invalid_key\":1},\n"

		// Unknown variable
		"{\"name\": \"unknown_var_op\", \"op\": \"no_var+1\","
					" \"unit\": \"%\", \"send\": 1},\n"

		// Unknown operation
		"{\"name\": \"unknown_op\", \"op\": \"no_var\\\\1\","
					" \"unit\": \"%\", \"send\": 1},\n"

		// Operation with timestamp involved (invalid), split and with
		// no monitors variables in operation
		"{\"name\": \"unknown_op\", \"op\": \"1+1\","
			"\"unit\": \"%\", \"send\": 1,"
			"\"split\":\";\",\"split_op\":\"sum\","
			"\"timestamp_given\":1},\n"

		// Operation with timestamp involved (invalid), timestamp
		// separation with no value
		"{\"name\": \"invalid_ts1\", \"system\": \"echo '1:'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\",\n"
			"\"timestamp_given\":1},\n"

		// Operation with timestamp involved (invalid), timestamp
		// separator with no timestamp
		"{\"name\": \"invalid_ts2\", \"system\": \"echo '1:2;7;3:4'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\",\n"
			"\"timestamp_given\":1},\n"

		// Operation with timestamp involved (invalid), timestamp
		// separator at the end of string
		"{\"name\": \"invalid_ts2\", \"system\": \"echo '1:2;7:4;3'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\",\n"
			"\"timestamp_given\":1},\n"

		// Can't convert number
		"{\"name\": \"v1\","
		"\"system\": \"echo '9e999999999999999999999999999999999999'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\"},\n"

		// operation over two vector of different sizes
		"{\"name\": \"v1\", \"system\": \"echo '0;1;2;3'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\"},\n"
		"{\"name\": \"v2\", \"system\": \"echo '0;1;2'\","
			" \"unit\": \"%\", \"send\": 0, \"split\":\";\"},\n"
		"{\"name\": \"v_op\", \"op\": \"v1+v2\","
			" \"unit\": \"%\", \"send\": 1},\n"

		// operation of scalar + vector
		"{\"name\": \"int\", \"system\": \"echo '0'\","
			" \"unit\": \"%\", \"send\": 0,},\n"
		"{\"name\": \"int_v_op\", \"int + v2\": \"\","
			" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"v_int_op\", \"v2 + int\": \"\","
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
static void prepare_invalid_monitor_checks(check_list_t *check_list) {
	struct json_key_test *checks[] = {
		TEST1_CHECKS("system","invalid_key","12.000000"),
		// TEST1_CHECKS("op","bad_var_op","1.000000"),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks),
								TEST1_SIZE);
}

/** Basic test */
TEST_FN(test_invalid_monitor, prepare_invalid_monitor_checks, invalid_monitor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_invalid_monitor),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
