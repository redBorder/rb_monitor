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

static const char split_op_sensor[] =  "{"
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
			"\"unit\": \"%\","
			"\"group_id\":\"1\",\"group_name\":\"first_group\"},"
		"{\"name\": \"load_5\", \"system\": \"echo ';6;8;10'\","
			"\"name_split_suffix\":\"_per_instance\","
			"\"split\":\";\",\"split_op\":\"mean\","
			"\"instance_prefix\":\"load-\",\"send\":0,"
			"\"unit\": \"%\","
			"\"group_id\":\"1\",\"group_name\":\"first_group\"},"
		"{\"name\": \"load_1+5\", \"op\": \"load_1+load_5\","
			"\"name_split_suffix\":\"_per_instance\","
			"\"split\":\";\",\"split_op\":\"mean\","
			"\"instance_prefix\":\"load-instance-\","
			"\"unit\": \"%\","
			"\"group_id\":\"1\",\"group_name\":\"first_group\"},"
	"]"
	"}";

#define TEST1_CHECKS0_V(mmonitor,mvalue)                                       \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type","op",                                                   \
	CHILD_S("unit","%",                                                    \
	CHILD_I("group_id",1,                                                  \
	CHILD_S("group_name","first_group", NULL))))))))

#define TEST1_CHECKS0_I(mmonitor,mvalue,minstance)                             \
	JSON_KEY_TEST(CHILD_S("instance",minstance,                            \
					TEST1_CHECKS0_V(mmonitor,mvalue)))

#define TEST1_CHECKS0_AVG(mmonitor,mvalue)                                     \
	JSON_KEY_TEST(TEST1_CHECKS0_V(mmonitor,mvalue))

static void prepare_split_op_monitor_checks(check_list_t *check_list) {
	json_key_test checks_v[] = {
		TEST1_CHECKS0_I("load_1+5_per_instance","8.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("load_1+5_per_instance","9.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("load_1+5_per_instance","10.000000",
							"load-instance-3")
	};

	json_key_test checks_op[] = {
		TEST1_CHECKS0_AVG("load_1+5", "9.000000")
	};

	check_list_push_checks(check_list, checks_v, RD_ARRAYSIZE(checks_v));
	check_list_push_checks(check_list, checks_op, 1);
}

/** @TODO merge with previous tests */
static void test_split_op_groups() {
	check_list_t checks = TAILQ_HEAD_INITIALIZER(checks);
	prepare_split_op_monitor_checks(&checks);

	test_sensor(split_op_sensor, &checks);
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op_groups),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
