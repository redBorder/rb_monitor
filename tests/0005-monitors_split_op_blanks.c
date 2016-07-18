#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char split_op_sensor_blanks[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\": \"echo ';2;1;0'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo ';6;8;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_1+5\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"

		"{\"name\": \"v1\", \"system\": \"echo ';12;11;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"v2\", \"system\": \"echo '14;16;;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"v1+v2\", \"op\": \"v1+v2\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"
	"]"
	"}";

#define TEST1_CHECKS0_V(mmonitor,mvalue)                                       \
	CHILD_I("sensor_id",1),                                                \
	CHILD_S("sensor_name","sensor-arriba"),                                \
	CHILD_S("monitor",mmonitor),                                           \
	CHILD_S("value",mvalue),                                               \
	CHILD_S("type","op"),                                                  \
	CHILD_S("unit","%")

#define TEST1_CHECKS0_I(mmonitor,mvalue,minstance) (struct json_key_test[]) {  \
	TEST1_CHECKS0_V(mmonitor,mvalue),                                      \
	CHILD_S("instance",minstance),                                         \
}

#define TEST1_CHECKS0_AVG(mmonitor,mvalue) (struct json_key_test[]) {          \
	TEST1_CHECKS0_V(mmonitor,mvalue)                                       \
}

#define TEST1_V_SAMPLE TEST1_CHECKS0_I("a","b","1")
#define TEST1_V_SIZE sizeof(TEST1_V_SAMPLE)/sizeof(TEST1_V_SAMPLE[0])

#define TEST1_AVG_SAMPLE TEST1_CHECKS0_AVG("a","b")
#define TEST1_AVG_SIZE sizeof(TEST1_AVG_SAMPLE)/sizeof(TEST1_AVG_SAMPLE[0])

static void prepare_split_op_monitor_blanks_checks(check_list_t *check_list) {
	struct json_key_test *checks_v[] = {
		TEST1_CHECKS0_I("load_1+5_per_instance","8.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("load_1+5_per_instance","9.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("load_1+5_per_instance","10.000000",
							"load-instance-3")
	};

	struct json_key_test *checks_op[] = {
		TEST1_CHECKS0_AVG("load_1+5", "9.000000")
	};

	check_list_push_checks(check_list, checks_v, RD_ARRAYSIZE(checks_v),
								TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_op, 1, TEST1_AVG_SIZE);

	struct json_key_test *checks_vars[] = {
		TEST1_CHECKS0_I("v1+v2_per_instance","14.000000",
							"load-instance-0"),
		TEST1_CHECKS0_I("v1+v2_per_instance","28.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("v1+v2_per_instance","11.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("v1+v2_per_instance","20.000000",
							"load-instance-3")
	};

	struct json_key_test *checks_vars_op[] = {
		TEST1_CHECKS0_AVG("v1+v2", "18.250000")
	};

	check_list_push_checks(check_list, checks_vars, RD_ARRAYSIZE(checks_vars),
								TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_vars_op, 1, TEST1_AVG_SIZE);
}

/** Test with blanks in instances */
TEST_FN(test_split_op_blanks, prepare_split_op_monitor_blanks_checks,
							split_op_sensor_blanks)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op_blanks),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
