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

#pragma once

#include "config.h"

#include "json_test.h"

/** Check cjson_sensor but only free returned messages
 * @param cjson_sensor Sensor in JSON format
 */
void test_sensor_void(const char *cjson_sensor);

/** Checks to pass a sensor n times
  @param cjson_sensor Sensor in json text format
  @param checks Checks to pass every time a sensor is processed
  @param n Number of checks (and times to pass)
  */
void test_sensor_n(const char *cjson_sensor, check_list_t *checks, size_t n);

/** Convenience function to pass checks over one sensor */
static void test_sensor(const char *cjson_sensor, check_list_t *checks)
							__attribute__((unused));
static void test_sensor(const char *cjson_sensor, check_list_t *checks) {
	test_sensor_n(cjson_sensor, checks, 1);
}

/** Basic sensor test
  @param prepare_checks_cb Construct checks using provided callback
  @param sensor JSON describing sensor under test
  */
static void basic_test_checks_cb(
		void (*prepare_checks_cb[])(check_list_t *checks),
		size_t checks_size, const char *sensor) __attribute__((unused));
static void basic_test_checks_cb(
		void (*prepare_checks_cb[])(check_list_t *checks),
		size_t checks_size, const char *sensor) {
	check_list_t checks[checks_size];
	for (size_t i=0; i<checks_size; ++i) {
		TAILQ_INIT(&checks[i]);
		prepare_checks_cb[i](&checks[i]);
	}
	test_sensor_n(sensor, checks, checks_size);
}

/// Convenience macro to create tests functions
#define TEST_FN_N(fn_name, prepare_checks_cb_v, n, json_sensor) \
static void fn_name() { \
	basic_test_checks_cb(prepare_checks_cb_v, n, json_sensor); \
}

/// Convenience macro to create tests functions
#define TEST_FN(fn_name, prepare_checks_cb, json_sensor) \
static void fn_name() { \
	typeof(prepare_checks_cb) *cb = &prepare_checks_cb; \
	basic_test_checks_cb(&cb, 1, json_sensor); \
}

extern size_t mem_wrap_fail_in;
