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

#include "rb_sensor_monitor_array.h"
#include "rb_sensor.h"
#include "rb_values_list.h"

#include <librd/rdlog.h>

#define rb_monitors_array_new(count) rb_array_new(count)
#define rb_monitors_array_full(array) rb_array_full(array)
/** @note Doing with a function provides type safety */
static void rb_monitors_array_add(rb_monitors_array_t *array,
							rb_monitor_t *monitor) {
	rb_array_add(array, monitor);
}

rb_monitor_t *rb_monitors_array_elm_at(rb_monitors_array_t *array, size_t i) {
	rb_monitor_t *ret = array->elms[i];
	assert_rb_monitor(ret);
	return ret;
}

rb_monitors_array_t *parse_rb_monitors(
				struct json_object *monitors_array_json) {
	const size_t monitors_len = json_object_array_length(
							monitors_array_json);
	rb_monitors_array_t *ret = rb_monitors_array_new(monitors_len);

	for (size_t i=0; i<monitors_len; ++i) {
		if (rb_monitors_array_full(ret)) {
			rdlog(LOG_CRIT,
				"Sensors array full at %zu, can't add %zu",
				ret->size, i);
			break;
		}

		json_object *monitor_json = json_object_array_get_idx(
							monitors_array_json, i);
		rb_monitor_t *monitor = parse_rb_monitor(monitor_json);
		if (monitor) {
			rb_monitors_array_add(ret, monitor);
		}
	}

	return ret;
}

/// @todo delete this FW declaration
struct rb_sensor_s;

static void process_monitors_array_values(const rb_monitor_t *monitor,
				struct rb_sensor_s *sensor,
				rb_monitor_value_array_t *monitor_values,
				rb_message_list *ret) {
	bool send = rb_monitor_send(monitor);

	for (size_t i=0; monitor_values && i<monitor_values->count; ++i) {
		const struct monitor_value *monitor_value =
							monitor_values->elms[i];

		const struct monitor_value *new_mv = update_monitor_value(
			rb_sensor_monitor_values_tree(sensor),monitor_value);

		if(send && new_mv) {
			rb_message_array_t* msgs = print_monitor_value(new_mv,
							monitor, sensor);
			if (msgs) {
				rb_message_list_push(ret, msgs);
			}
		}
	}
}

bool process_monitors_array(struct _worker_info *worker_info,
			rb_sensor_t *sensor, rb_monitors_array_t *monitors,
			struct snmp_params_s *snmp_params,
			rb_message_list *ret) {
	bool aok = true;
	struct monitor_snmp_session *snmp_sessp = NULL;
	struct process_sensor_monitor_ctx *process_ctx = NULL;
	/* @todo we only need this if we are going to use SNMP */
	if (NULL == snmp_params->peername) {
		aok = false;
		rdlog(LOG_ERR, "Peername not setted in %s. Skipping.",
							rb_sensor_name(sensor));
	}

	if (NULL == snmp_params->session.community) {
		aok = false;
		rdlog(LOG_ERR, "Community not setted in %s. Skipping.",
							rb_sensor_name(sensor));
	}

	if(aok) {
		pthread_mutex_lock(&worker_info->snmp_session_mutex);
		/* @TODO: You can do it later, see session_api.h */
		worker_info->default_session.peername =
						(char *)snmp_params->peername;
		const struct monitor_snmp_new_session_config config = {
			snmp_params->session.community,
			snmp_params->session.timeout,
			worker_info->default_session.flags,
			snmp_params->session.version
		};
		snmp_sessp = new_snmp_session(&worker_info->default_session,&config);
		if(NULL== snmp_sessp){
			rdlog(LOG_ERR,"Error creating session: %s",snmp_errstring(worker_info->default_session.s_snmp_errno));
			aok=0;
		}
		pthread_mutex_unlock(&worker_info->snmp_session_mutex);

		process_ctx = new_process_sensor_monitor_ctx(monitors->count,
								snmp_sessp);
	}

	for (size_t i=0; aok && i<monitors->count; ++i) {
		const rb_monitor_t *monitor = rb_monitors_array_elm_at(monitors,
									i);
		rb_monitor_value_array_t *vals = process_sensor_monitor(
					process_ctx, monitor, sensor);
		process_monitors_array_values(monitor, sensor, vals, ret);
	}

	if (process_ctx) {
		destroy_process_sensor_monitor_ctx(process_ctx);
	}

	if (snmp_sessp) {
		destroy_snmp_session(snmp_sessp);
	}

	return aok;
}

void rb_monitors_array_done(rb_monitors_array_t *monitors_array) {
	for (size_t i=0; i<monitors_array->count; ++i) {
		rb_monitor_done(rb_monitors_array_elm_at(monitors_array, i));
	}
	free(monitors_array);
}
