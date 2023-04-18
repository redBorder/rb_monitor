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

static const char split_sensor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		/* Split with no suffix */
		"{\"name\": \"load_1_ns\", \"system\": \"echo '3;2;1;0'\","
				"\"split\":\";\",\"unit\": \"%\"},"
		"{\"name\": \"load_5_ns\", \"system\": \"echo '4;5;6;7'\","
				"\"split\":\";\",\"unit\": \"%\"},"

		/* Split with suffix */
		"{\"name\": \"load_1\", \"system\": \"echo '3;2;1;0'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo '4;5;6;7'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"unit\": \"%\"},"
	"]"
	"}";

#define TEST_CHECKS(mmonitor,mvalue)                                           \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type","system",                                               \
	CHILD_S("unit","%",NULL))))))

#define TEST_LOAD_1_CHECKS0(monitor) \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"3.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"2.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"1.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"0.000000"))

#define TEST_LOAD_5_CHECKS0(monitor) \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"4.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"5.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"6.000000")), \
	JSON_KEY_TEST(TEST_CHECKS(monitor,"7.000000"))

#define TEST_LOAD1_SUFFIX TEST_LOAD_1_CHECKS0("load_1_per_instance")
#define TEST_LOAD1_NO_SUFFIX TEST_LOAD_1_CHECKS0("load_1_ns")
#define TEST_LOAD5_SUFFIX TEST_LOAD_5_CHECKS0("load_5_per_instance")
#define TEST_LOAD5_NO_SUFFIX TEST_LOAD_5_CHECKS0("load_5_ns")

static void prepare_split_monitor_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		TEST_LOAD1_NO_SUFFIX,
		TEST_LOAD5_NO_SUFFIX,
		TEST_LOAD1_SUFFIX,
		TEST_LOAD5_SUFFIX
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));
}

/** Basic split monitor */
TEST_FN(test_split, prepare_split_monitor_checks, split_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
