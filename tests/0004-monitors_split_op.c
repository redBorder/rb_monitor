#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char split_op_sensor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\": \"echo '3;2;1;0'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo '4;6;8;10'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"

		/* mean split op of a vector operation */
		"{\"name\": \"load_1+5_mean\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"

		/* sum split op of a vector operation */
		"{\"name\": \"load_1+5_sum\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"

		/* invalid split op of a vector operation */
		/* (not sending anything at this moment) */
		"{\"name\": \"load_1+5_sum\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"invalid\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"

		/* do not send split op */
		"{\"name\": \"load_1+5_sum\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\", \"send\":0},"
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

static void prepare_split_op_monitor_checks(check_list_t *check_list) {
	struct json_key_test *checks_v_avg[] = {
		TEST1_CHECKS0_I("load_1+5_mean_per_instance","7.000000",
							"load-instance-0"),
		TEST1_CHECKS0_I("load_1+5_mean_per_instance","8.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("load_1+5_mean_per_instance","9.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("load_1+5_mean_per_instance","10.000000",
							"load-instance-3")
	};

	struct json_key_test *checks_op_avg[] = {
		TEST1_CHECKS0_AVG("load_1+5_mean", "8.500000")
	};

	check_list_push_checks(check_list, checks_v_avg,
				RD_ARRAYSIZE(checks_v_avg), TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_op_avg, 1, TEST1_AVG_SIZE);

	struct json_key_test *checks_v_sum[] = {
		TEST1_CHECKS0_I("load_1+5_sum_per_instance","7.000000",
							"load-instance-0"),
		TEST1_CHECKS0_I("load_1+5_sum_per_instance","8.000000",
							"load-instance-1"),
		TEST1_CHECKS0_I("load_1+5_sum_per_instance","9.000000",
							"load-instance-2"),
		TEST1_CHECKS0_I("load_1+5_sum_per_instance","10.000000",
							"load-instance-3")
	};

	struct json_key_test *checks_op_sum[] = {
		TEST1_CHECKS0_AVG("load_1+5_sum", "34.000000")
	};

	check_list_push_checks(check_list, checks_v_sum,
				RD_ARRAYSIZE(checks_v_sum), TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_op_sum, 1, TEST1_AVG_SIZE);
}

/** Basic split monitor test, with sum/mean op */
TEST_FN(test_split_op, prepare_split_op_monitor_checks, split_op_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
