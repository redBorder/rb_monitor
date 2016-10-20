#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include "rb_sensor.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

// clang-format off

static const char *invalid_sensors[] = {
		/* No monitors */
		"{\n"
			"\"sensor_id\":1,\n"
			"\"timeout\":100000000000000000000000,\n"
			"\"sensor_name\": \"sensor-arriba\",\n"
			"\"sensor_ip\": \"localhost\",\n"
			"\"community\" : \"public\",\n"
		"}",
	};

void test_invalid_sensors() {
	struct _worker_info worker_info;
        memset(&worker_info, 0, sizeof(worker_info));
        snmp_sess_init(&worker_info.default_session);


	for (size_t i=0; i<RD_ARRAYSIZE(invalid_sensors); ++i) {
		const char *text_sensor = invalid_sensors[i];
	        struct json_object *json_sensor = json_tokener_parse(text_sensor);
	        rb_sensor_t *sensor = parse_rb_sensor(json_sensor, &worker_info);
	        assert_null(sensor);
	        json_object_put(json_sensor);
	}
}

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_invalid_sensors),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}
