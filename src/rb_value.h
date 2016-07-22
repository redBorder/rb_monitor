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

#include "rb_array.h"
#include "rb_message_list.h"

#include <signal.h>
#include <pthread.h>
#include <librd/rdtypes.h>
#include <librd/rdmem.h>

#include <stdbool.h>
#include <json-c/json.h>

#ifndef NDEBUG
#define MONITOR_VALUE_MAGIC 0x010AEA1C010AEA1CL
#endif

/// @todo make the vectors entry here.
/// @note if you edit this structure, remember to edit monitor_value_copy
struct monitor_value {
	#ifdef MONITOR_VALUE_MAGIC
	uint64_t magic; // Private data, don't need to use them outside.
	#endif

	/// Type of monitor value
	enum monitor_value_type {
		/// This is a raw value
		MONITOR_VALUE_T__VALUE,
		/// This is an array of monitors values
		MONITOR_VALUE_T__ARRAY,
	} type;

	/* response */
	union {
		struct {
			time_t timestamp;
			double value;
			bool bad_value;
			const char *string_value;
		} value;
		struct {
			size_t children_count;
			struct monitor_value *split_op_result;
			struct monitor_value **children;
		} array;
	};
};

struct monitor_value *new_monitor_value_array(const char *name,
		size_t n_children, struct monitor_value **children,
		struct monitor_value *split_op);

#ifdef MONITOR_VALUE_MAGIC
#define rb_monitor_value_assert(monitor) \
	assert(MONITOR_VALUE_MAGIC == (monitor)->magic)
#else
#define rb_monitor_value_assert(monitor)
#endif

void rb_monitor_value_done(struct monitor_value *mv);

/** Sensors array */
typedef struct rb_array rb_monitor_value_array_t;

/** Create a new array with count capacity */
#define rb_monitor_value_array_new(sz) rb_array_new(sz)

/** Destroy a sensors array */
#define rb_monitor_value_array_done(array) rb_array_done(array)

/** Checks if a monitor value array is full */
#define rb_monitor_value_array_full(array) rb_array_full(array)

/** Add a monitor value to monitor values array
  @note Wrapper function allows typechecking */
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
					struct monitor_value *mv) RD_UNUSED;
static void rb_monitor_value_array_add(rb_monitor_value_array_t *array,
					struct monitor_value *mv) {
	rb_array_add(array, mv);
}

/** Select individual positions of original array
  @param array Original array
  @param pos list of positions (-1 terminated)
  @return New monitor array, that needs to be free with
        rb_monitor_value_array_done
  @note Monitors are from original array, so they should not be touched
  */
rb_monitor_value_array_t *rb_monitor_value_array_select(
		rb_monitor_value_array_t *array, ssize_t *pos);

/** Return monitor value of an array
  @param array Array
  @param i Position of array
  @return Monitor value at position i
  */
static struct monitor_value *rb_monitor_value_array_at(
				rb_monitor_value_array_t *array, size_t i)
							__attribute__((unused));
static struct monitor_value *rb_monitor_value_array_at(
				rb_monitor_value_array_t *array, size_t i) {
	struct monitor_value *ret = array->elms[i];
	if (ret) {
		rb_monitor_value_assert(ret);
	}
	return ret;
}

/// @todo delete this FW declarations, print should not be here
struct rb_monitor_s;
struct rb_sensor_s;

/** Print a sensor value
  @param monitor_value Value to print
  @param monitor Value's monitor
  @param sensor  Monitor's sensor
  @param prev_timestamp Do not print monitor value instance if instance
         timestamp is below this value (if provided)
  @return new printbuf with result string (has to be freed with )
  */
rb_message_array_t *print_monitor_value(
 	const struct monitor_value *monitor_value,
	const struct rb_monitor_s *monitor, const struct rb_sensor_s *sensor);

/** Compare monitor's timestamp
  @param m1 First monitor to compare
  @param m2 Second monitor to compare
  @warning assert that both monitor value are type value
  @return integer lees than, equal to, o greater than 0 if m1 timestamp is less
  than, equal to, o greater than m2 timestamp
  */
static int64_t rb_monitor_value_cmp_timestamp(const struct monitor_value *m1,
			const struct monitor_value *m2) __attribute__((unused));
static int64_t rb_monitor_value_cmp_timestamp(const struct monitor_value *m1,
			const struct monitor_value *m2) {
	return m1->value.timestamp - m2->value.timestamp;
}
