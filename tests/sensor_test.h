#include "config.h"

#include "json_test.h"

#define UNDER_TEST
#include "main.c"

/// @TODO sepparate in .c file when we can extract struct definitions!
static void test_sensor(const char *cjson_sensor, check_list_t *checks) {
	struct _worker_info worker_info;
	struct _perthread_worker_info pt_worker_info;
	rd_lru_t *messages = rd_lru_new();

	memset(&worker_info, 0, sizeof(worker_info));
	memset(&pt_worker_info, 0, sizeof(pt_worker_info));

	worker_info.monitor_values_tree = new_monitor_values_tree();
	snmp_sess_init(&worker_info.default_session);
	struct json_object *json_sensor = json_tokener_parse(cjson_sensor);

	process_sensor(&worker_info, &pt_worker_info, json_sensor, messages);
	json_list_check(checks, messages);
	json_object_put(json_sensor);

	destroy_monitor_values_tree(worker_info.monitor_values_tree);
	rd_lru_destroy(messages);
}

/** Basic sensor test
  @param prepare_checks_cb Construct checks using provided callback
  @param sensor JSON describing sensor under test
  */
static void basic_test_checks_cb(
		void (*prepare_checks_cb)(check_list_t *checks),
		const char *sensor) __attribute__((unused));
static void basic_test_checks_cb(
		void (*prepare_checks_cb)(check_list_t *checks),
		const char *sensor) {
	check_list_t checks = TAILQ_HEAD_INITIALIZER(checks);
	prepare_checks_cb(&checks);
	test_sensor(sensor, &checks);
}

/// Convenience macro to create tests functions
#define TEST_FN(fn_name, prepare_checks_cb, json_sensor) \
static void fn_name() {basic_test_checks_cb(prepare_checks_cb, json_sensor);}
