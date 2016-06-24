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
#include <librd/rdfloat.h>

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

/// Swap two pointers
#define SWAP(T, a, b) do { T tmp = a; a = b; b = tmp; } while (0)
/** Print all elements of new array that have changed
  @param monitor_value New monitor value
  @param old_mv Previous monitor value we had
  @return Monitor messages to send
  */
static rb_message_array_t *process_monitor_value_v(const rb_monitor_t *monitor,
					const rb_sensor_t *sensor,
					struct monitor_value *new_mv,
					struct monitor_value *old_mv) {
	assert(new_mv->type == MONITOR_VALUE_T__ARRAY);
	assert(old_mv->type == MONITOR_VALUE_T__ARRAY);

	struct monitor_value *print_children[new_mv->array.children_count];
	memset(print_children, 0,
			new_mv->array.children_count*sizeof(print_children[0]));

	/* Print all values that have changed */
	size_t i=0;
	for (i=0; i<new_mv->array.children_count &&
					i<old_mv->array.children_count; ++i) {
		struct monitor_value *new_mv_i = new_mv->array.children[i];
		const struct monitor_value *old_mv_i
						= old_mv->array.children[i];

		if (new_mv_i) {
			const bool update = (NULL == old_mv_i
				|| rb_monitor_value_cmp_timestamp(old_mv_i,
								new_mv_i) < 0
				|| rd_dne(old_mv_i->value.value,
						new_mv_i->value.value));

			if (update) {
				print_children[i] = new_mv_i;
			}
		}
	}

	/* Print all new variables, if any */
	for(; i<new_mv->array.children_count; ++i) {
		print_children[i] = new_mv->array.children[i];
	}

	struct monitor_value to_print = {
#ifdef MONITOR_VALUE_MAGIC
		.magic = MONITOR_VALUE_MAGIC,
#endif
		.type = MONITOR_VALUE_T__ARRAY,
		.name = new_mv->name,
		.group_id = new_mv->group_id,
		.array = {
			.children_count = new_mv->array.children_count,
			.split_op_result = new_mv->array.split_op_result,
			.children = print_children,
		},
	};

	rb_message_array_t *ret = print_monitor_value(&to_print, monitor,
									sensor);
	SWAP(void *,old_mv->array.children, new_mv->array.children);
	rb_monitor_value_done(new_mv);
	return ret;
}

static void process_monitor_value(const rb_monitor_t *monitor,
				struct rb_sensor_s *sensor,
				struct monitor_value *monitor_value,
				rb_message_list *ret) {
	assert(monitor_value);

	rb_message_array_t* msgs = NULL;
	struct monitor_values_tree *mv_tree =
					rb_sensor_monitor_values_tree(sensor);

	struct monitor_value *old_mv = find_monitor_value(mv_tree,
		rb_monitor_name(monitor), rb_monitor_group_id(monitor));

	const bool updated_value = NULL == old_mv ||
		(monitor_value->type == MONITOR_VALUE_T__VALUE &&
			rb_monitor_timestamp_provided(monitor) &&
			rb_monitor_value_cmp_timestamp(old_mv,
							monitor_value) < 0) ||
		(monitor_value->type == MONITOR_VALUE_T__ARRAY &&
			!rb_monitor_timestamp_provided(monitor));

	if (updated_value) {
		add_monitor_value(mv_tree, monitor_value);

		if(rb_monitor_send(monitor)) {
			msgs = print_monitor_value(monitor_value, monitor,
									sensor);
		}

		if (old_mv) {
			rb_monitor_value_done(old_mv);
		}
	} else if (monitor_value->type == MONITOR_VALUE_T__ARRAY) {
		msgs = process_monitor_value_v(monitor, sensor, monitor_value,
									old_mv);
	} else {
		// No use for the new monitor value
		rb_monitor_value_done(monitor_value);
	}

	if (msgs) {
		rb_message_list_push(ret, msgs);
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
		struct monitor_value *value = process_sensor_monitor(
					process_ctx, monitor, sensor);
		if (value) {
			process_monitor_value(monitor, sensor, value, ret);
		}
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
