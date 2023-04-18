/*
  Copyright (C) 2016 Eneo Tecnologia S.L.
  Author: Eugenio Perez <eupm90@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include "sensor_test.h"

#include "rb_sensor.h"

static void test_exec_sensor_cb(const char *cjson_sensor,
				void (*msg_cb)(void *opaque,
					       rb_message_list *msgs,
					       size_t i),
				void *opaque,
				size_t n) {
	const size_t aux_mem_wrap_fail_in = mem_wrap_fail_in; // Exclude this
							      // code
	mem_wrap_fail_in = 0;
	struct _worker_info worker_info;
	memset(&worker_info, 0, sizeof(worker_info));

	snmp_sess_init(&worker_info.default_session);
	struct json_object *json_sensor = json_tokener_parse(cjson_sensor);
	rb_sensor_t *sensor = parse_rb_sensor(json_sensor, &worker_info);
	json_object_put(json_sensor);

	mem_wrap_fail_in = aux_mem_wrap_fail_in;
	for (size_t i = 0; i < n; ++i) {
		rb_message_list messages;
		rb_message_list_init(&messages);
		process_rb_sensor(&worker_info, sensor, &messages);
		msg_cb(opaque, &messages, i);
	}
	rb_sensor_put(sensor);
}

static void test_sensor_n_cb(void *vchecks, rb_message_list *msgs, size_t i) {
	check_list_t *checks = vchecks;
	json_list_check(&checks[i], msgs);
}

void test_sensor_n(const char *cjson_sensor, check_list_t *checks, size_t n) {
	test_exec_sensor_cb(cjson_sensor, test_sensor_n_cb, checks, n);
}

static void test_sensor_void_cb(void *opaque, rb_message_list *msgs, size_t i) {
	(void)opaque;
	(void)i;

	while (!(rb_message_list_empty(msgs))) {
		rb_message_array_t *array = rb_message_list_first(msgs);
		rb_message_list_remove(msgs, array);
		for (size_t j = 0; j < array->count; ++j) {
			free(array->msgs[j].payload);
		}

		message_array_done(array);
	}
}

void test_sensor_void(const char *cjson_sensor) {
	test_exec_sensor_cb(cjson_sensor, test_sensor_void_cb, NULL, 1);
}

/* malloc / calloc fails tests */

size_t mem_wrap_fail_in = 0;

void mem_wraps_set_fail_in(size_t i) {
	mem_wrap_fail_in = i;
}

size_t mem_wraps_get_fail_in() {
	return mem_wrap_fail_in;
}

#define COMMA ,

#define WRAP_MEM_FN(fun, ret_t, args, real_args)                               \
	ret_t __real_##fun(args);                                              \
	ret_t __wrap_##fun(args);                                              \
	ret_t __wrap_##fun(args) {                                             \
		return (mem_wrap_fail_in == 0 || --mem_wrap_fail_in)           \
				       ? __real_##fun(real_args)               \
				       : 0;                                    \
	}

WRAP_MEM_FN(malloc, void *, size_t m, m)
WRAP_MEM_FN(calloc, void *, size_t n COMMA size_t m, n COMMA m)
WRAP_MEM_FN(__strdup, char *, const char *str, str)
WRAP_MEM_FN(strdup, char *, const char *str, str)
WRAP_MEM_FN(json_object_new_object, json_object *, , )
WRAP_MEM_FN(json_object_new_string, json_object *, const char *str, str)
WRAP_MEM_FN(json_object_new_int64, json_object *, , )
WRAP_MEM_FN(printbuf_new, struct printbuf *, , )
WRAP_MEM_FN(evaluator_create, void *, char *str, str)
