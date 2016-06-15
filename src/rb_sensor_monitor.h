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

#include "rb_snmp.h"

#include <json/json.h>
#include <stdbool.h>

#include <librd/rdlru.h>

/* FW declaration */
struct rb_sensor_s;
struct _worker_info;

/// Single monitor
typedef struct rb_monitor_s rb_monitor_t;

/// Monitors array
typedef struct rb_array rb_monitors_array_t;

/// SNMP connection parameters
struct snmp_params_s {
	/// Peername to connect
	const char *peername;
	/// Connection values
	struct monitor_snmp_new_session_config session;
};

/** Gets monitor instance_prefix
  @param monitor Monitor to get data
  @return requested data
  */
const char *rb_monitor_instance_prefix(const rb_monitor_t *monitor);

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

/** Extract monitors array from a JSON array.
  @param monitors_array_json JSON monitors template
  @param sensor Sensor this monitor's belong
  @return New monitors array
  @note Need to free returned monitors with rb_monitors_array_done
  */
rb_monitors_array_t *parse_rb_monitors(struct json_object *monitors_array_json,
	struct rb_sensor_s *sensor);

/** Process all monitors in sensor, returning result in ret
  @param worker_info All workers info
  @param sensor_data Data of current sensor
  @param monitors Array of monitors to ask
  @param snmp_params SNMP connection parameters
  @param ret Message returning function
  @warning This function assumes ALL fields of sensor_data will be populated */
bool process_monitors_array(struct _worker_info * worker_info,
		struct rb_sensor_s *sensor, rb_monitors_array_t *monitors_array,
		struct snmp_params_s *snmp_params,
		rd_lru_t *ret);

/** Free array allocated with parse_rb_monitors
  @param array Array
  */
void rb_monitors_array_done(rb_monitors_array_t *array);
