#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

#define TIME_VARIABLE_CMD(pid_prefix, FIRST_ACTION, SECOND_ACTION)             \
	"test -f \\\"" pid_prefix "$PPID.pid\\\" && "                          \
	"(" SECOND_ACTION "; rm \\\"" pid_prefix "$PPID.pid\\\";) || "         \
	"(" FIRST_ACTION "; touch \\\"" pid_prefix "$PPID.pid\\\";)"

#define LOAD_5_OP                                                              \
	TIME_VARIABLE_CMD("load-5",                                            \
			  "echo '10:20;30:40'",                                \
			  "echo "                                              \
			  "'10:20;60:40'")
#define V_INCR_END                                                             \
	TIME_VARIABLE_CMD("v-increase-end",                                    \
			  "echo '10:20;'",                                     \
			  "echo '10:20;30:40'")
#define V_INCR_STA                                                             \
	TIME_VARIABLE_CMD("v-increase-start",                                  \
			  "echo ';30:40'",                                     \
			  "echo '10:20;30:40'")
#define V_DECR_END                                                             \
	TIME_VARIABLE_CMD("v-decrease-end",                                    \
			  "echo '10:20;30:40'",                                \
			  "echo '10:20;'")
#define V_DECR_STA                                                             \
	TIME_VARIABLE_CMD("v-decrease-start",                                  \
			  "echo '10:20;30:40'",                                \
			  "echo ';30:40'")
#define V_INCR_N                                                               \
	TIME_VARIABLE_CMD("v-increase-n",                                      \
			  "echo '10:20'",                                      \
			  "echo "                                              \
			  "'10:20;30:40'")
#define V_DECR_N                                                               \
	TIME_VARIABLE_CMD("v-decrease-n",                                      \
			  "echo '10:20;30:40'",                                \
			  "echo "                                              \
			  "'30:80'")
#define V_DIFF_V                                                               \
	TIME_VARIABLE_CMD("v-diferent-v",                                      \
			  "echo '10:20;30:40'",                                \
			  "echo '10:20;30:80'")

// clang-format off

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

		/* Vector that will be tail increased at second pass */
		"{\"name\": \"v-increase-end\", \"system\": \"" V_INCR_END "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vie-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector that will be tail decreased at second pass */
		"{\"name\": \"v-decrease-end\", \"system\": \"" V_DECR_END "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vde-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector that will be head increased at second pass */
		"{\"name\": \"v-increase-start\", \"system\": \"" V_INCR_STA "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vis-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector that will be head decresed at second pass */
		"{\"name\": \"v-decrease-start\", \"system\": \"" V_DECR_STA "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vds-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector that will have one more element at second pass */
		"{\"name\": \"v-increase-n\", \"system\": \"" V_INCR_N "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vin-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector that will have one less element at second pass */
		"{\"name\": \"v-decrease-n\", \"system\": \"" V_DECR_N "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vdn-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"

		/* Vector with same timestamp but different value */
		"{\"name\": \"v-different-v\", \"system\": \"" V_DIFF_V "\","
				"\"name_split_suffix\":\"_per_instance\","
				"\"split\":\";\",\"split_op\":\"mean\","
				"\"instance_prefix\":\"vdv-\","
				"\"unit\": \"%\","
				"\"timestamp_given\":1},"
	"]"
	"}";

#define TEST1_CHECKS0_V(mmonitor,mvalue,mtype)                                 \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	CHILD_S("value",mvalue,                                                \
	CHILD_S("type",mtype,                                                  \
	CHILD_S("unit","%", NULL))))))

#define TEST1_CHECKS0_I0(mmonitor,mvalue,mtype,minstance)                      \
	CHILD_S("instance",minstance, TEST1_CHECKS0_V(mmonitor,mvalue,mtype))

#define TEST1_CHECKS0_V_OP(mmonitor,mvalue,mtype,minstance)                    \
	JSON_KEY_TEST(TEST1_CHECKS0_I0(mmonitor,mvalue,mtype,minstance))

#define TEST1_CHECKS0_SYSTEM(mtimestamp,mmonitor,mvalue,minstance)             \
	JSON_KEY_TEST(CHILD_I("timestamp",mtimestamp,                          \
		TEST1_CHECKS0_I0(mmonitor,mvalue,"system",minstance)))

#define TEST1_CHECKS0_SPLIT_OP(mmonitor,mvalue,mtype)                          \
	JSON_KEY_TEST(TEST1_CHECKS0_V(mmonitor,mvalue,mtype))

static void prepare_op_checks(check_list_t *check_list) {
	json_key_test checks_v[] = {
		TEST1_CHECKS0_V_OP("load_1+5_per_instance","22.000000", "op",
							"load-instance-0"),
		TEST1_CHECKS0_V_OP("load_1+5_per_instance","44.000000", "op",
							"load-instance-1"),
	};

	json_key_test checks_op[] = {
		TEST1_CHECKS0_SPLIT_OP("load_1+5", "33.000000", "op")
	};

	check_list_push_checks(check_list, checks_v, RD_ARRAYSIZE(checks_v));
	check_list_push_checks(check_list, checks_op, 1);
}

static void prepare_split_op_timestamp(check_list_t *check_list) {
	/* V5 checks */
	json_key_test checks_v5[] = {
		TEST1_CHECKS0_SYSTEM(10, "load_5_per_instance","20.000000",
							"load5-0"),
		TEST1_CHECKS0_SYSTEM(30, "load_5_per_instance","40.000000",
							"load5-1"),
	};

	json_key_test checks_op5[] = {
		TEST1_CHECKS0_SPLIT_OP("load_5", "30.000000", "system")
	};

	check_list_push_checks(check_list, checks_v5, RD_ARRAYSIZE(checks_v5));
	check_list_push_checks(check_list, checks_op5, 1);

	/* Ops */
	prepare_op_checks(check_list);

	/* v-increase-end */
	json_key_test checks_inc_end[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-increase-end_per_instance",
						"20.000000", "vie-0"),
	};

	json_key_test checks_op_inc_end[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-end", "20.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_end,
						RD_ARRAYSIZE(checks_inc_end));
	check_list_push_checks(check_list, checks_op_inc_end, 1);

	/* v-decrease-end */
	json_key_test checks_dec_end[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-decrease-end_per_instance",
						"20.000000", "vde-0"),
		TEST1_CHECKS0_SYSTEM(30, "v-decrease-end_per_instance",
						"40.000000", "vde-1"),
	};

	json_key_test checks_op_dec_end[] = {
		TEST1_CHECKS0_SPLIT_OP("v-decrease-end", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_dec_end,
						RD_ARRAYSIZE(checks_dec_end));
	check_list_push_checks(check_list, checks_op_dec_end, 1);

	/* v-increase-start */
	json_key_test checks_inc_sta[] = {
		TEST1_CHECKS0_SYSTEM(30, "v-increase-start_per_instance",
						"40.000000", "vis-1"),
	};

	json_key_test checks_op_inc_sta[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-start", "40.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_sta,
						RD_ARRAYSIZE(checks_inc_sta));
	check_list_push_checks(check_list, checks_op_inc_sta, 1);

	/* v-decrease-start */
	json_key_test checks_dec_start[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-decrease-start_per_instance",
						"20.000000", "vds-0"),
		TEST1_CHECKS0_SYSTEM(30, "v-decrease-start_per_instance",
						"40.000000", "vds-1"),
	};

	json_key_test checks_op_dec_start[] = {
		TEST1_CHECKS0_SPLIT_OP("v-decrease-start", "30.000000",
								"system"),
	};

	check_list_push_checks(check_list, checks_dec_start,
						RD_ARRAYSIZE(checks_dec_start));
	check_list_push_checks(check_list, checks_op_dec_start, 1);

	/* v-increase-n */
	json_key_test checks_inc_n[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-increase-n_per_instance",
						"20.000000", "vin-0"),
	};

	json_key_test checks_op_inc_n[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-n", "20.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_n,
						RD_ARRAYSIZE(checks_inc_n));
	check_list_push_checks(check_list, checks_op_inc_n, 1);

	/* v-decrease-n */
	json_key_test checks_dec_n[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-decrease-n_per_instance",
						"20.000000", "vdn-0"),
		TEST1_CHECKS0_SYSTEM(30, "v-decrease-n_per_instance",
						"40.000000", "vdn-1"),
	};

	json_key_test checks_op_dec_n[] = {
		TEST1_CHECKS0_SPLIT_OP("v-decrease-n", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_dec_n,
						RD_ARRAYSIZE(checks_dec_n));
	check_list_push_checks(check_list, checks_op_dec_n, 1);

	/* v-different-v */
	json_key_test checks_diff_v[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-different-v_per_instance",
						"20.000000", "vdv-0"),
		TEST1_CHECKS0_SYSTEM(30, "v-different-v_per_instance",
						"40.000000", "vdv-1"),
	};

	json_key_test checks_op_diff_v[] = {
		TEST1_CHECKS0_SPLIT_OP("v-different-v", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_diff_v,
						RD_ARRAYSIZE(checks_diff_v));
	check_list_push_checks(check_list, checks_op_diff_v, 1);
}

static void prepare_split_op_timestamp_2(check_list_t *check_list) {
	/* load-5 */
	json_key_test checks_v5_after_ts_change[] = {
		/*
		Only one!
		TEST1_CHECKS0_SYSTEM(10, "load_5_per_instance","20.000000",
							"load5-0"),
		*/
		TEST1_CHECKS0_SYSTEM(60, "load_5_per_instance","40.000000",
							"load5-1"),
	};

	json_key_test checks_op5_after_ts_change[] = {
		TEST1_CHECKS0_SPLIT_OP("load_5", "30.000000", "system")
	};

	/* After timestamp change */
	check_list_push_checks(check_list, checks_v5_after_ts_change,
		RD_ARRAYSIZE(checks_v5_after_ts_change));
	check_list_push_checks(check_list, checks_op5_after_ts_change, 1);

	/* op */
	prepare_op_checks(check_list);

	/* v-increase-start */
	/* After vector increase, only the 2nd change timestamp */
	json_key_test checks_inc_end[] = {
		TEST1_CHECKS0_SYSTEM(30, "v-increase-end_per_instance",
						"40.000000", "vie-1"),
	};

	json_key_test checks_op_inc_end[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-end", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_end,
				RD_ARRAYSIZE(checks_inc_end));
	check_list_push_checks(check_list, checks_op_inc_end, 1);

	/* v-decrease-end */
	/* No new array message */

	json_key_test checks_op_dec_end[] = {
		/* 20 because the previous value deletion! */
		TEST1_CHECKS0_SPLIT_OP("v-decrease-end", "20.000000", "system"),
	};

	check_list_push_checks(check_list, checks_op_dec_end, 1);

	/* v-increase-start */
	/* After vector increase, only the 1st change timestamp */
	json_key_test checks_inc_sta[] = {
		TEST1_CHECKS0_SYSTEM(10, "v-increase-start_per_instance",
						"20.000000", "vis-0"),
	};

	json_key_test checks_op_inc_sta[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-start", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_sta,
				RD_ARRAYSIZE(checks_inc_sta));
	check_list_push_checks(check_list, checks_op_inc_sta, 1);

	/* v-decrease-start */
	/* No new array message */

	json_key_test checks_op_dec_start[] = {
		/* 40 because the previous value deletion! */
		TEST1_CHECKS0_SPLIT_OP("v-decrease-start", "40.000000", "system"),
	};

	check_list_push_checks(check_list, checks_op_dec_start, 1);

	/* v-increase-n */
	json_key_test checks_inc_n[] = {
		TEST1_CHECKS0_SYSTEM(30, "v-increase-n_per_instance",
						"40.000000", "vin-1"),
	};

	json_key_test checks_op_inc_n[] = {
		TEST1_CHECKS0_SPLIT_OP("v-increase-n", "30.000000", "system"),
	};

	check_list_push_checks(check_list, checks_inc_n,
						RD_ARRAYSIZE(checks_inc_n));
	check_list_push_checks(check_list, checks_op_inc_n, 1);

	/* v-decrease-n */
	json_key_test checks_dec_n[] = {
		TEST1_CHECKS0_SYSTEM(30, "v-decrease-n_per_instance",
						"80.000000", "vdn-0"),
	};
	check_list_push_checks(check_list, checks_dec_n,
						RD_ARRAYSIZE(checks_dec_n));

	json_key_test checks_op_dec_n[] = {
		TEST1_CHECKS0_SPLIT_OP("v-decrease-n", "80.000000", "system"),
	};

	check_list_push_checks(check_list, checks_op_dec_n, 1);

	/* v-different-v */
	json_key_test checks_diff_v[] = {
		TEST1_CHECKS0_SYSTEM(30, "v-different-v_per_instance",
						"80.000000", "vdv-1"),
	};

	json_key_test checks_op_diff_v[] = {
		TEST1_CHECKS0_SPLIT_OP("v-different-v", "50.000000", "system"),
	};

	check_list_push_checks(check_list, checks_diff_v,
						RD_ARRAYSIZE(checks_diff_v));
	check_list_push_checks(check_list, checks_op_diff_v, 1);
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
