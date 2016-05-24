#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

static const char split_op_sensor_empty[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\": \"echo ';;;'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo ';;;'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\"},"
		"{\"name\": \"load_1+5\", \"op\": \"load_1+load_5\","
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

static void prepare_split_op_monitor_empty_checks(check_list_t *check_list) {
	(void)check_list;
}

/** Test ops with no data */
TEST_FN(test_split_op_empty, prepare_split_op_monitor_empty_checks,
							split_op_sensor_empty)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op_empty),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
