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

#include "rb_value.h"
#include "rb_snmp.h"

#include <json-c/json.h>

#include <stdbool.h>

/* FW declaration */
struct rb_sensor_s;
struct _worker_info;

/// Single monitor
typedef struct rb_monitor_s rb_monitor_t;

#ifdef NDEBUG
#define assert_rb_monitor(monitor)
#else
void assert_rb_monitor(const rb_monitor_t *monitor);
#endif

/// Context to process all monitors
struct process_sensor_monitor_ctx;

/** Parse a rb_monitor element
  @param json_monitor monitor in JSON format
  @return Parsed rb_monitor.
  */
rb_monitor_t *parse_rb_monitor(struct json_object *json_monitor);

/** Free resources allocated by a monitor
  @param monitor Monitor to free
  */
void rb_monitor_done(rb_monitor_t *monitor);

/** Creates a new monitor process ctx
  @param monitors_count # of monitors
  @param snmp_sessp Session to make SNMP request
  @return New monitor process ctx
  */
struct process_sensor_monitor_ctx *new_process_sensor_monitor_ctx(
                                size_t monitors_count,
                                struct monitor_snmp_session *snmp_sessp);

/** Destroy process sensor monitor context
  @param ctx Context to free
  */
void destroy_process_sensor_monitor_ctx(struct process_sensor_monitor_ctx *ctx);

/// @todo delete this FW declaration
struct rb_sensor_s;

/** Process a sensor monitor
  @param process_ctx Process context
  @param monitor Monitor to process
  @param op_vars Variables that require operations
  @param ret Returned messages
  */
struct monitor_value *process_sensor_monitor(
				struct process_sensor_monitor_ctx *process_ctx,
				const rb_monitor_t *monitor,
				rb_monitor_value_array_t *op_vars);

/** Gets if monitor expect timestamp
  @param monitor Monitor to get data
  @return requested data
  */
bool rb_monitor_timestamp_provided(const rb_monitor_t *monitor);

/** Gets monitor instance_prefix
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_instance_prefix(const rb_monitor_t *monitor);

/** Gets monitor name split suffix
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_name_split_suffix(const rb_monitor_t *monitor);

/** Gets monitor name
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_name(const rb_monitor_t *monitor);

/** Gets monitor group_id
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_group_id(const rb_monitor_t *monitor);

/** Gets monitor group_name
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_group_name(const rb_monitor_t *monitor);

/** Gets monitor integer status
  @param monitor Monitor to get data
  @return requested data
  */
bool rb_monitor_is_integer(const rb_monitor_t *monitor);

/** Gets monitor type
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_type(const rb_monitor_t *monitor);

/** Gets monitor unit
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_unit(const rb_monitor_t *monitor);

/** Gets monitor send variable
  @param monitor Monitor to get data
  @return requested data
  */
bool rb_monitor_send(const rb_monitor_t *monitor);

/** Gets monitor operation (snmp, system, op...) param
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_get_cmd_data(const rb_monitor_t *monitor);

/** Gets monitor operation needed variables
  @param monitor Monitor to get data
  @param vars Char array to store vars
  @param vars_size length of store vars
  @return true if success, false in other case
  */
void rb_monitor_get_op_variables(const rb_monitor_t *monitor,char ***vars,
							size_t *vars_size);

/** Free data returned by rb_monitor_op_variables_get
  @param vars Variables
  @param vars_size Length of vars
  */
void rb_monitor_free_op_variables(char **vars, size_t vars_size);
