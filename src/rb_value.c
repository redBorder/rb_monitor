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

#include "rb_value.h"

#include "rb_sensor.h"
#include "rb_sensor_monitor.h"

#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <json-c/printbuf.h>

struct monitor_value *new_monitor_value_array(const char *name,
		size_t n_children, struct monitor_value **children,
		struct monitor_value *split_op) {
	struct monitor_value *ret = calloc(1, sizeof(*ret));
	if (NULL == ret) {
		if (split_op) {
			rb_monitor_value_done(split_op);
		}
		for (size_t i=0; i<n_children; ++i) {
			if (children[i]) {
				rb_monitor_value_done(children[i]);
			}
		}
		free(children);

		rdlog(LOG_ERR, "Couldn't allocate monitor value");
		return NULL;
	}

#ifdef MONITOR_VALUE_MAGIC
	ret->magic = MONITOR_VALUE_MAGIC;
#endif

	ret->type = MONITOR_VALUE_T__ARRAY;
	ret->array.children_count = n_children;
	ret->array.split_op_result = split_op;
	ret->array.children = children;

	return ret;
}

static void print_monitor_value_enrichment_str(struct printbuf *printbuf,
					const char *key, json_object *val) {
	const char *str = json_object_get_string(val);
	if(NULL == str) {
		rdlog(LOG_ERR,
			"Cannot extract string value of enrichment key %s",
			key);
	} else {
		sprintbuf(printbuf, ",\"%s\":\"%s\"",
			key,str);
	}
}

static void print_monitor_value_enrichment_int(struct printbuf *printbuf,
					const char *key, json_object *val) {
	errno = 0;
	int64_t integer = json_object_get_int64(val);
	if(errno != 0) {
		char errbuf[BUFSIZ];
		const char *errstr = strerror_r(errno, errbuf, sizeof(errbuf));
		rdlog(LOG_ERR,
			"Cannot extract int value of enrichment key %s: %s",
			key, errstr);
	} else {
		sprintbuf(printbuf, ",\"%s\":%ld", key, integer);
	}
}

/// @TODO we should print all with this function
static void print_monitor_value_enrichment(struct printbuf *printbuf,
					const json_object *const_enrichment) {
	json_object *enrichment = (json_object *)const_enrichment;

	for (struct json_object_iterator i = json_object_iter_begin(enrichment),
					end = json_object_iter_end(enrichment);
					!json_object_iter_equal(&i, &end);
					json_object_iter_next(&i)) {
		const char *key = json_object_iter_peek_name(&i);
		json_object *val = json_object_iter_peek_value(&i);

		const json_type type = json_object_get_type(val);
		switch(type){
			case json_type_string:
				print_monitor_value_enrichment_str(
					printbuf, key, val);
				break;

			case json_type_int:
				print_monitor_value_enrichment_int(
					printbuf, key, val);
				break;

			case json_type_null:
				sprintbuf(printbuf, ",\"%s\":null",key);
				break;

			case json_type_boolean:
			{
				const json_bool b = json_object_get_boolean(val);
				sprintbuf(printbuf, ",\"%s\":%s", key,
						b==FALSE ? "false" : "true");
				break;
			}
			case json_type_double:
			{
				const double d = json_object_get_double(val);
				sprintbuf(printbuf, ",\"%s\":%lf",key,d);
				break;
			}
			case json_type_object:
			case json_type_array:
			{
				rdlog(LOG_ERR,
					"Can't enrich with objects/array at this time");
				break;
			}
		};
	}
}

#define NO_INSTANCE -1
static void print_monitor_value0(rb_message *message,
			const struct monitor_value *monitor_value,
			const rb_monitor_t *monitor, int instance) {
	assert(monitor_value->type == MONITOR_VALUE_T__VALUE);

	struct printbuf * printbuf = printbuf_new();
	if(likely(NULL!=printbuf)) {
		const char *monitor_instance_prefix =
					rb_monitor_instance_prefix(monitor);
		const char *monitor_name_split_suffix =
					rb_monitor_name_split_suffix(monitor);
		const struct json_object *monitor_enrichment =
						rb_monitor_enrichment(monitor);
		// @TODO use printbuf_memappend_fast instead! */
		sprintbuf(printbuf, "{");
		sprintbuf(printbuf, "\"timestamp\":%lu",
						monitor_value->value.timestamp);
		if (NO_INSTANCE != instance && monitor_name_split_suffix) {
			sprintbuf(printbuf, ",\"monitor\":\"%s%s\"",
						rb_monitor_name(monitor),
						monitor_name_split_suffix);
		} else {
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",
						rb_monitor_name(monitor));
		}

		if (NO_INSTANCE != instance && monitor_instance_prefix) {
			sprintbuf(printbuf, ",\"instance\":\"%s%d\"",
					monitor_instance_prefix, instance);
		}

		if (rb_monitor_is_integer(monitor)) {
			sprintbuf(printbuf, ",\"value\":%"PRId64,
					(int64_t)monitor_value->value.value);
		} else {
			sprintbuf(printbuf, ",\"value\":\"%lf\"",
						monitor_value->value.value);
		}

		if (rb_monitor_group_id(monitor)) {
			sprintbuf(printbuf, ",\"group_id\":%s",
			rb_monitor_group_id(monitor));
		}


		if (monitor_enrichment) {
			print_monitor_value_enrichment(printbuf,
							monitor_enrichment);
		}
		sprintbuf(printbuf, "}");

		message->payload = printbuf->buf;
		message->len = printbuf->bpos;

		printbuf->buf = NULL;
		printbuf_free(printbuf);

	}
}

rb_message_array_t *print_monitor_value(
		const struct monitor_value *monitor_value,
		const rb_monitor_t *monitor, const rb_sensor_t *sensor) {
	const size_t ret_size = monitor_value->type == MONITOR_VALUE_T__VALUE ?
		1 : monitor_value->array.children_count +
			(monitor_value->array.split_op_result ? 1 : 0);

	rb_message_array_t *ret = new_messages_array(ret_size);
	if (ret == NULL) {
		rdlog(LOG_ERR, "Couldn't allocate messages array");
		return NULL;
	}

	if (monitor_value->type == MONITOR_VALUE_T__VALUE) {
		print_monitor_value0(&ret->msgs[0], monitor_value, monitor,
			NO_INSTANCE);
	} else {
		size_t i_msgs = 0;
		assert(monitor_value->type == MONITOR_VALUE_T__ARRAY);
		for (size_t i=0; i<monitor_value->array.children_count; ++i) {
			if (monitor_value->array.children[i]) {
				print_monitor_value0(&ret->msgs[i_msgs++],
					monitor_value->array.children[i],
					monitor, i);
			}
		}

		if (monitor_value->array.split_op_result) {
			rb_message *msg = &ret->msgs[i_msgs++];
			assert(NULL == msg->payload);
			print_monitor_value0(msg,
				monitor_value->array.split_op_result, monitor,
				NO_INSTANCE);
		}

		ret->count = i_msgs;
	}

	return ret;
}

static ssize_t pos_array_length(ssize_t *pos) {
	assert(pos);
	ssize_t i = 0;
	for (i=0; -1 != pos[i]; ++i);
	return i;
}

rb_monitor_value_array_t *rb_monitor_value_array_select(
				rb_monitor_value_array_t *array, ssize_t *pos) {
	if (NULL == pos || NULL == array) {
		return NULL;
	}

	const size_t ret_size = pos_array_length(pos);
	rb_monitor_value_array_t *ret = rb_monitor_value_array_new(ret_size);
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate select return (OOM?)");
		return NULL;
	}


	assert(array);
	assert(pos);

	for (ssize_t i=0; -1 != pos[i]; ++i) {
		rb_monitor_value_array_add(ret, array->elms[pos[i]]);
	}

	return ret;
}

void rb_monitor_value_done(struct monitor_value *mv) {
	if (MONITOR_VALUE_T__ARRAY == mv->type) {
		for (size_t i=0; i<mv->array.children_count; ++i) {
			if (mv->array.children[i]) {
				rb_monitor_value_done(mv->array.children[i]);
			}
		}
		if (mv->array.split_op_result) {
			rb_monitor_value_done(mv->array.split_op_result);
		}
		free(mv->array.children);
	}
	free(mv);
}
