#include "config.h"

#include "rb_sensor.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <string.h>
#include <stdarg.h>
#include <setjmp.h>
#include <cmocka.h>

/// Just include enough stuff to use all c/mallocs in rb_monitor!
static const char basic_sensor[] = "{\n"
	"\"sensor_id\":1,"
	"\"timeout\":2,"
	"\"sensor_name\": \"sensor-arriba\","
	"\"sensor_ip\": \"localhost\","
	"\"community\" : \"public\","
	"\"monitors\": /* this field MUST be the last! */"
	"["
#if 0
		"{\"name\": \"load_1\", \"system\":  \"echo 1\","
					"\"unit\": \"%\", \"send\": 0},"
		"{\"name\": \"load_5\", \"system\": \"echo 3\","
							"\"unit\": \"%\"},"
		"{\"name\": \"load_15\", \"system\": \"echo 2\","
					"\"unit\": \"%\", \"send\": 1},"

		"// This line will also be sent\n"
		"{\"name\": \"no_unit\", \"system\": \"echo 5\","
					"\"send\": 1},\n"

		"{\"name\": \"load_5_x_load_1\", \"op\":\"load_5*load_1\","
							"\"unit\": \"%\"},"
#endif
		"{\"name\": \"vector\", \"system\":\"echo '1;2;3'\","
			"\"split\":\";\",\"split_op\":\"mean\","
			"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"
#if 0
		"{\"name\": \"2vector\", \"op\":\"vector+vector\","
			"\"split\":\";\",\"split_op\":\"mean\","
			"\"instance_prefix\":\"load-instance-\","
				"\"unit\": \"%\"},"
#endif
	"]"
	"}";

static void mem_test(void (*cb)(const char *sensor_str),
						const char *sensor_str) {
	size_t i = 1;
	do {
		mem_wrap_fail_in = i++;
		cb(sensor_str);
	} while (0 == mem_wrap_fail_in);
	mem_wrap_fail_in = 0;
}

static void mem_parse_sensor(const char *sensor_str) {
	const size_t aux_mem_wrap_fail_in = mem_wrap_fail_in; // Exclude this code
	mem_wrap_fail_in = 0;
	struct _worker_info worker_info;
	memset(&worker_info, 0, sizeof(worker_info));

	snmp_sess_init(&worker_info.default_session);
	struct json_object *json_sensor = json_tokener_parse(sensor_str);
	mem_wrap_fail_in = aux_mem_wrap_fail_in;
	rb_sensor_t *sensor = parse_rb_sensor(json_sensor, &worker_info);
	json_object_put(json_sensor);

	if (sensor) {
		rb_sensor_put(sensor);
	}
}

/// @test try to parse a sensor with memory allocation errors
static void test_parsing_sensor_mem() {
	mem_test(mem_parse_sensor, basic_sensor);
}


static void test_basic_sensor_mem() {
	mem_test(test_sensor_void, basic_sensor);
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_parsing_sensor_mem),
		cmocka_unit_test(test_basic_sensor_mem),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
