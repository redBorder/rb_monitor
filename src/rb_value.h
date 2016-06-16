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

#include "rb_sensor_monitor.h"
#include "rb_sensor.h"
#include "rb_array.h"

#include <signal.h>
#include <pthread.h>
#include <librd/rdtypes.h>
#include <librd/rdlru.h>
#include <librd/rdavl.h>
#include <librd/rdmem.h>

#include <stdbool.h>
#include <json/json.h>

#ifndef NDEBUG
#define MONITOR_VALUE_MAGIC 0x010AEA1C010AEA1CL
#endif

/// @todo make the vectors entry here.
/// @note if you edit this structure, remember to edit monitor_value_copy
struct monitor_value{
	rd_memctx_t memctx;
	rd_avl_node_t avl_node;

	#ifdef MONITOR_VALUE_MAGIC
	uint64_t magic; // Private data, don't need to use them outside.
	#endif

	/* config.json extracted */
	const char * sensor_name;

	const char * name;            // Intern name: *__gid__*__pos__
	const char * send_name;       // Extern name. If not __gid__ nor __pos__, it is NULL and you have to check name.
	                              // @todo make a function name() for do the last.
	const char * instance_prefix;
	const char * group_id;

	/* response */
	time_t timestamp;
	double value;
	const char *string_value;

	/* vector response */
	unsigned int instance;
	bool instance_valid;
	bool bad_value;
};

void rb_monitor_value_done(struct monitor_value *mv);

void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src);

int process_monitor_value(struct monitor_value *monitor_value);

/** Sensors array */
typedef struct rb_array rb_monitor_value_array_t;

/** Create a new array with count capacity */
#define rb_monitor_value_array_new(sz) rb_array_new(sz)

/** Destroy a sensors array */
#define rb_monitor_value_array_done(array) rb_array_done(array)

/** Checks if a sensor array is full */
#define rb_monitor_value_array_full(array) rb_array_full(array)

/** Add a sensor to sensors array
  @note Wrapper function allows typechecking */
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
					struct monitor_value *sensor) RD_UNUSED;
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
					struct monitor_value *sensor) {
	rb_array_add(array, sensor);
}

/** Print a sensor value
  @param monitor_value Value to print
  @param monitor Value's monitor
  @param sensor  Monitor's sensor
  @return new printbuf with result string (has to be freed with )
  */
struct printbuf *print_monitor_value(const struct monitor_value *monitor_value,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor);
