#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

#define LOAD_5_OP "[[ -f $PPID.pid ]] && " \
	"(echo '10:20;60:40'; rm $PPID.pid;) || " \
	"(echo '10:20;30:40'; touch $PPID.pid;)"

/** Sensor with timestamp given in response */
static const char split_op_timestamp[] =  "{"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
		"{\"name\": \"load_1\", \"system\": \"echo '1:2;3:4'\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"sum\","
				"\"instance_prefix\":\"load-\",\"send\":0,"
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"
		"{\"name\": \"load_5\", \"system\": \"" LOAD_5_OP "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load5-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"
		"{\"name\": \"load_1+5\", \"op\": \"load_1+load_5\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"
	"]"
	"}";

#define TEST1_CHECKS0_V(mmonitor,mvalue,mtype)                                 \
	CHILD_I("sensor_id",1),                                                \
	CHILD_S("sensor_name","sensor-arriba"),                                \
	CHILD_S("monitor",mmonitor),                                           \
	CHILD_S("value",mvalue),                                               \
	CHILD_S("type",mtype),                                                 \
	CHILD_S("unit","%")

#define TEST1_CHECKS0_I0(mmonitor,mvalue,mtype,minstance)                      \
		TEST1_CHECKS0_V(mmonitor,mvalue,mtype),                        \
		CHILD_S("instance",minstance)

#define TEST1_CHECKS0_V_OP(mmonitor,mvalue,mtype,minstance)                    \
	(struct json_key_test[]) {                                             \
		TEST1_CHECKS0_I0(mmonitor,mvalue,mtype,minstance)              \
	}

#define TEST1_CHECKS0_SYSTEM(mtimestamp,mmonitor,mvalue,minstance)             \
	(struct json_key_test[]) {                                             \
		CHILD_I("timestamp",mtimestamp),                               \
		TEST1_CHECKS0_I0(mmonitor,mvalue,"system",minstance)           \
	}

#define TEST1_CHECKS0_SPLIT_OP(mmonitor,mvalue,mtype)                          \
	(struct json_key_test[]) {                                             \
		TEST1_CHECKS0_V(mmonitor,mvalue,mtype)                         \
	}

#define TEST1_V_SYSTEM_SAMPLE TEST1_CHECKS0_SYSTEM(1,"a","b","1")
#define TEST1_V_SIZE \
	sizeof(TEST1_V_SYSTEM_SAMPLE)/sizeof(TEST1_V_SYSTEM_SAMPLE[0])

#define TEST1_V_OP_SAMPLE TEST1_CHECKS0_V_OP("mon","val","type","instance")
#define TEST1_V_OP_SIZE sizeof(TEST1_V_OP_SAMPLE)/sizeof(TEST1_V_OP_SAMPLE[0])

#define TEST1_SPLIT_OP_SAMPLE TEST1_CHECKS0_SPLIT_OP("a","b","c")
#define TEST1_SPLIT_OP_SIZE \
		sizeof(TEST1_SPLIT_OP_SAMPLE)/sizeof(TEST1_SPLIT_OP_SAMPLE[0])

static void prepare_op_checks(check_list_t *check_list) {
	struct json_key_test *checks_v[] = {
		TEST1_CHECKS0_V_OP("load_1+5_per_instance","22.000000", "op",
							"load-instance-0"),
		TEST1_CHECKS0_V_OP("load_1+5_per_instance","44.000000", "op",
							"load-instance-1"),
	};

	struct json_key_test *checks_op[] = {
		TEST1_CHECKS0_SPLIT_OP("load_1+5", "33.000000", "op")
	};

	check_list_push_checks(check_list, checks_v, RD_ARRAYSIZE(checks_v),
							TEST1_V_OP_SIZE);
	check_list_push_checks(check_list, checks_op, 1, TEST1_SPLIT_OP_SIZE);
}

static void prepare_split_op_timestamp(check_list_t *check_list) {
	struct json_key_test *checks_v5[] = {
		TEST1_CHECKS0_SYSTEM(10, "load_5_per_instance","20.000000",
							"load5-0"),
		TEST1_CHECKS0_SYSTEM(30, "load_5_per_instance","40.000000",
							"load5-1"),
	};

	struct json_key_test *checks_op5[] = {
		TEST1_CHECKS0_SPLIT_OP("load_5", "30.000000", "system")
	};

	check_list_push_checks(check_list, checks_v5, RD_ARRAYSIZE(checks_v5),
							TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_op5, 1, TEST1_SPLIT_OP_SIZE);

	prepare_op_checks(check_list);
}

static void prepare_split_op_timestamp_2(check_list_t *check_list) {
	struct json_key_test *checks_v5_after_ts_change[] = {
		/*
		Only one!
		TEST1_CHECKS0_SYSTEM(10, "load_5_per_instance","20.000000",
							"load5-0"),
		*/
		TEST1_CHECKS0_SYSTEM(60, "load_5_per_instance","40.000000",
							"load5-1"),
	};

	struct json_key_test *checks_op5_after_ts_change[] = {
		TEST1_CHECKS0_SPLIT_OP("load_5", "30.000000", "system")
	};

	/* After timestamp change */
	check_list_push_checks(check_list, checks_v5_after_ts_change,
		RD_ARRAYSIZE(checks_v5_after_ts_change), TEST1_V_SIZE);
	check_list_push_checks(check_list, checks_op5_after_ts_change,
							1, TEST1_SPLIT_OP_SIZE);

	prepare_op_checks(check_list);
}

void (*prepare_cb[])(check_list_t *) = {
	prepare_split_op_timestamp,
	prepare_split_op_timestamp_2,
};

TEST_FN_N(test_split_op_timestamp, prepare_cb, RD_ARRAYSIZE(prepare_cb),
							split_op_timestamp);

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_split_op_timestamp),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
