/*
  Copyright (C) 2015 Eneo Tecnologia S.L.
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


void test_sensor(const char *cjson_sensor, check_list_t *checks);

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
