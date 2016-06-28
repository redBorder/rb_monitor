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

/** Prints a monitor value taking into account timestamp of values
  @param monitor Monitor of monitor value
  @param sensor Sensor Sensor of monitor
  @param new_mv New monitor value
  @param old_mv Old monitor value
  @return Message array of this update
  */
static rb_message_array_t *process_monitor_value_v_print(
					const rb_monitor_t *monitor,
					const rb_sensor_t *sensor,
					const struct monitor_value *new_mv,
					const struct monitor_value *old_mv) {
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

	return print_monitor_value(&to_print, monitor, sensor);
}

/// Swap two pointers
#define SWAP(a, b) do { typeof(a) tmp = a; a = b; b = tmp; } while (0)

/** Print all elements of new array that have changed
  @param monitor Monitor of monitors values
  @param sensor Sensor of monitor
  @param new_mv New monitor value
  @param old_mv Previous monitor value we had
  @return Monitor messages to send
  */
static rb_message_array_t *process_monitor_value_v(const rb_monitor_t *monitor,
					const rb_sensor_t *sensor,
					struct monitor_value *new_mv,
					struct monitor_value *old_mv) {
	rb_message_array_t *ret = NULL;

	assert(new_mv->type == MONITOR_VALUE_T__ARRAY);
	assert(old_mv->type == MONITOR_VALUE_T__ARRAY);

	if (rb_monitor_send(monitor)) {
		ret = process_monitor_value_v_print(monitor, sensor, new_mv,
									old_mv);
	}

	SWAP(old_mv->array.children_count, new_mv->array.children_count);
	SWAP(old_mv->array.children, new_mv->array.children);
	rb_monitor_value_done(new_mv);
	return ret;
}

/** Process a monitor value
  @param monitor Monitor this monitor value is related
  @param sensor  Sensor of the monitor
  @param monitor_value New monitor value to process
  @param old_mv Last known monitor value
  @param ret Message list to report
  @return New monitor value we should save
  */
static struct monitor_value *process_monitor_value(const rb_monitor_t *monitor,
				struct rb_sensor_s *sensor,
				struct monitor_value *monitor_value,
				struct monitor_value *old_mv,
				rb_message_list *ret) {
	assert(monitor_value);

	rb_message_array_t* msgs = NULL;
	struct monitor_value *ret_mv = old_mv;

	const bool update_value = NULL == old_mv
				|| !rb_monitor_timestamp_provided(monitor)
				|| (monitor_value->type
						== MONITOR_VALUE_T__VALUE &&
					rb_monitor_value_cmp_timestamp(old_mv,
							monitor_value) < 0);

	if (update_value) {
		if(rb_monitor_send(monitor)) {
			msgs = print_monitor_value(monitor_value, monitor,
									sensor);
		}

		if (old_mv) {
			rb_monitor_value_done(old_mv);
		}
		ret_mv = monitor_value;
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

	return ret_mv;
}

bool process_monitors_array(struct _worker_info *worker_info,
			rb_sensor_t *sensor, rb_monitors_array_t *monitors,
			rb_monitor_value_array_t *last_known_monitor_values,
			ssize_t **monitors_deps,
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
		rb_monitor_value_array_t *op_vars =
			rb_monitor_value_array_select(last_known_monitor_values,
					monitors_deps[i]);

		const rb_monitor_t *monitor = rb_monitors_array_elm_at(monitors,
									i);
		struct monitor_value *value = process_sensor_monitor(
					process_ctx, monitor, op_vars);
		if (value) {
			struct monitor_value *last_known_monitor_value_i =
					last_known_monitor_values->elms[i];

			last_known_monitor_values->elms[i] =
				process_monitor_value(monitor, sensor, value,
					last_known_monitor_value_i, ret);
		}

		rb_monitor_value_array_done(op_vars);
	}

	for (size_t i=0; aok && i<monitors->count; ++i) {
		/* We don't need monitors with no timestamp information in it,
		so we delete them */
		const rb_monitor_t *monitor = rb_monitors_array_elm_at(monitors,
									i);
		if (last_known_monitor_values->elms[i] &&
				!rb_monitor_timestamp_provided(monitor)) {
			rb_monitor_value_done(
					last_known_monitor_values->elms[i]);
			last_known_monitor_values->elms[i] = NULL;
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

/** Get a monitor position
  @param monitors_array Array of monitors
  @param name Name of monitor to find
  @param group_id Group ip of monitor
  @return position of the monitor, or -1 if it couldn't be found
  */
static ssize_t find_monitor_pos(const rb_monitors_array_t *monitors_array,
				const char *name, const char *group_id) {
	for (size_t i=0; i<monitors_array->count; ++i) {
		const char *i_name = rb_monitor_name(monitors_array->elms[i]);
		const char *i_gid =
				rb_monitor_group_id(monitors_array->elms[i]);
		if (0 == strcmp(name, i_name) &&
					((!i_gid && !group_id)
					|| 0 == strcmp(group_id, i_gid))) {
			return (ssize_t)i;
		}
	}

	return -1;
}

/** Retuns a -1 terminated array with monitor operations variables position
  @param monitors_array Array of monitors
  @param monitor Monitor to search for
  @return requested array
  */
static ssize_t *get_monitor_dependencies(
				const rb_monitors_array_t *monitors_array,
				const rb_monitor_t *monitor) {
	ssize_t *ret = NULL;
	char **vars;
	size_t vars_len;

	rb_monitor_get_op_variables(monitor, &vars, &vars_len);

	if (vars_len > 0) {
		ret = calloc(vars_len + 1, sizeof(ret[0]));
		if (NULL == ret) {
			rdlog(LOG_ERR, "Couldn't allocate dependencies array "
								"(OOM?)");
			goto err;
		}

		for (size_t i=0; i<vars_len; ++i) {
			ret[i] = find_monitor_pos(monitors_array, vars[i],
						rb_monitor_group_id(monitor));
			if (-1 == ret[i]) {
				rdlog(LOG_ERR, "Couldn't find variable [%s] in"
					"operation [%s]. Discarding",
					vars[i],
					rb_monitor_get_cmd_data(monitor));
				free(ret);
				ret = NULL;
				goto err;
			}
		}

		ret[vars_len] = -1;
	}

err:
	rb_monitor_free_op_variables(vars, vars_len);
	return ret;
}

ssize_t **get_monitors_dependencies(const rb_monitors_array_t *monitors_array) {
	ssize_t **ret = calloc(monitors_array->count, sizeof(ret[0]));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate monitor dependences!");
		return NULL;
	}

	for (size_t i=0; i<monitors_array->count; ++i) {
		const rb_monitor_t *i_monitor = monitors_array->elms[i];
		ret[i] = get_monitor_dependencies(monitors_array, i_monitor);
	}

	return ret;
}

void free_monitors_dependencies(ssize_t **deps, size_t count) {
	for (size_t i=0; i<count; ++i) {
		free(deps[i]);
	}
	free(deps);
}

void rb_monitors_array_done(rb_monitors_array_t *monitors_array) {
	for (size_t i=0; i<monitors_array->count; ++i) {
		rb_monitor_done(rb_monitors_array_elm_at(monitors_array, i));
	}
	free(monitors_array);
}
