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

static const char ops_monitor[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		/* Group 1 */
		"{\"name\": \"load_1\", \"system\": \"echo 13\","
			"\"unit\": \"%\", \"group_id\":1, \"group_name\":\"g1\"},"
		"{\"name\": \"load_5\", \"system\": \"echo 12\","
			"\"unit\": \"%\", \"group_id\":1, \"group_name\":\"g1\"},"
		"// Operation\n"
		"{\"name\": \"100load_5\", \"op\":\"100*load_5\","
			"\"unit\": \"%\", \"group_id\":1, \"group_name\":\"g1\"},"
		"// 2 variables operation\n"
		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
			"\"unit\": \"%\", \"group_id\":1, \"group_name\":\"g1\"},"

		/* No group */
		"{\"name\": \"load_1\", \"system\": \"echo 23\","
			"\"unit\": \"%\"},"
		"{\"name\": \"load_5\", \"system\": \"echo 22\","
			"\"unit\": \"%\"},"
		"// Operation\n"
		"{\"name\": \"100load_5\", \"op\":\"100*load_5\","
			"\"unit\": \"%\"},"
		"// 2 variables operation\n"
		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
			"\"unit\": \"%\"},"

		/* Group 2 */
		"{\"name\": \"load_1\", \"system\": \"echo 33\","
			"\"unit\": \"%\", \"group_id\":2, \"group_name\":\"g2\"},"
		"{\"name\": \"load_5\", \"system\": \"echo 32\","
			"\"unit\": \"%\", \"group_id\":2, \"group_name\":\"g2\"},"
		"// Operation\n"
		"{\"name\": \"100load_5\", \"op\":\"100*load_5\","
			"\"unit\": \"%\", \"group_id\":2, \"group_name\":\"g2\"},"
		"// 2 variables operation\n"
		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
			"\"unit\": \"%\", \"group_id\":2, \"group_name\":\"g2\"},"

	"]"
	"}";

#define TEST_CHECKS0(mmonitor,mvalue,mtype)                                    \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type",mtype,                                                  \
	CHILD_S("unit","%", NULL))))))

#define TEST_CHECKS(mmonitor,mvalue,mtype)                                     \
	JSON_KEY_TEST(TEST_CHECKS0(mmonitor,mvalue,mtype))

#define TEST_CHECKSG(mmonitor,mvalue,mtype,gid,gn) JSON_KEY_TEST(              \
	CHILD_I("group_id",gid,                                                \
	CHILD_S("group_name",gn,                                               \
	TEST_CHECKS0(mmonitor,mvalue,mtype))))

static void prepare_math_ops_checks(check_list_t *check_list) {
	enum {G1, NO_GROUP, G2};

	json_key_test checks_G1[] = {
		TEST_CHECKSG("load_1","13.000000","system",1,"g1"),
		TEST_CHECKSG("load_5","12.000000","system",1,"g1"),
		TEST_CHECKSG("100load_5","1200.000000","op",1,"g1"),
		TEST_CHECKSG("load_5_x_load_1","156.000000","op",1,"g1"),
	};

	json_key_test checks_NO_GROUP[] = {
		TEST_CHECKS("load_1","23.000000","system"),
		TEST_CHECKS("load_5","22.000000","system"),
		TEST_CHECKS("100load_5","2200.000000","op"),
		TEST_CHECKS("load_5_x_load_1","506.000000","op"),
	};

	json_key_test checks_G2[] = {
		TEST_CHECKSG("load_1","33.000000","system",2,"g2"),
		TEST_CHECKSG("load_5","32.000000","system",2,"g2"),
		TEST_CHECKSG("100load_5","3200.000000","op",2,"g2"),
		TEST_CHECKSG("load_5_x_load_1","1056.000000","op",2,"g2"),
	};

	check_list_push_checks(check_list, checks_G1, RD_ARRAYSIZE(checks_G1));
	check_list_push_checks(check_list, checks_NO_GROUP,
						RD_ARRAYSIZE(checks_NO_GROUP));
	check_list_push_checks(check_list, checks_G2, RD_ARRAYSIZE(checks_G2));

}

/** Basic test */
TEST_FN(test_ops, prepare_math_ops_checks, ops_monitor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_ops),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
