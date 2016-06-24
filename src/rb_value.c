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
		rdlog(LOG_ERR, "Couldn't allocate monitor value");
		return NULL;
	}

#ifdef MONITOR_VALUE_MAGIC
	ret->magic = MONITOR_VALUE_MAGIC;
#endif

	ret->type = MONITOR_VALUE_T__ARRAY;
	ret->name = name;
	ret->array.children_count = n_children;
	ret->array.split_op_result = split_op;
	ret->array.children = children;

	return ret;
}

/* Copy just the 'useful' data of the node, not list-related */
void monitor_value_copy(struct monitor_value *dst,
					const struct monitor_value *src) {
	assert(src);
	assert(dst);

	memset(dst, 0, sizeof(*dst));

#ifdef MONITOR_VALUE_MAGIC
	dst->magic = MONITOR_VALUE_MAGIC;
#endif

        if(src->name) {
                dst->name      = strdup(src->name);
        }
        if(src->group_id) {
		dst->group_id  = strdup(src->group_id);
	}

        switch(src->type) {
        case MONITOR_VALUE_T__VALUE:
        	dst->value.timestamp = src->value.timestamp;
        	dst->value.bad_value = src->value.bad_value;
        	dst->value.value     = src->value.value;
		if(src->value.string_value) {
			dst->value.string_value = strdup(src->value.string_value);
		}

        	break;
	case MONITOR_VALUE_T__ARRAY:
		dst->array.children_count = src->array.children_count;
		if (src->array.split_op_result) {
			dst->array.split_op_result = calloc(1,
					sizeof(dst->array.split_op_result));
			if (dst->array.split_op_result) {
				monitor_value_copy(dst->array.split_op_result,
					src->array.split_op_result);
			} else {
				/// @TODO error treatment
			}
		}

		for (size_t i=0; i<dst->array.children_count; ++i) {
			monitor_value_copy(dst->array.children[i],
					src->array.children[i]);
		}
		break;
        };
}

/// @TODO we should print all with this function
static void print_monitor_value_enrichment(struct printbuf *printbuf,const json_object *_enrichment)
{
	json_object *enrichment = (json_object *)_enrichment;

	char *key; struct json_object *val; struct lh_entry *entry;
	for(entry = json_object_get_object(enrichment)->head;
		(entry ? (key = (char*)entry->k, val = (struct json_object*)entry->v, entry) : 0);
		entry = entry->next){
	//json_object_object_foreach(enrichment,key,val) {
		const json_type type = json_object_get_type(val);
		switch(type){
			case json_type_string:
			{
				const char *str = json_object_get_string(val);
				if(NULL == str) {
					rdlog(LOG_ERR,"Cannot extract string value of enrichment key %s",key);
				} else {
					sprintbuf(printbuf, ",\"%s\":\"%s\"",key,str);
				}
				break;
			}
			case json_type_int:
			{
				errno = 0;
				int64_t integer = json_object_get_int64(val);
				if(errno != 0) {
					rdlog(LOG_ERR,"Cannot extract int value of enrichment key %s",key);
				} else {
					sprintbuf(printbuf, ",\"%s\":%ld",key,integer);
				}
				break;
			}
			case json_type_null:
			{
				sprintbuf(printbuf, ",\"%s\":null",key);
				break;
			}
			case json_type_boolean:
			{
				const json_bool b = json_object_get_boolean(val);
				sprintbuf(printbuf, ",\"%s\":%s",key,b==FALSE ? "false" : "true");
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
				rdlog(LOG_ERR,"Can't enrich with objects/array at this time");
				break;
			}
		};
	}
}

#define NO_INSTANCE -1
static void print_monitor_value0(rb_message *message,
			const struct monitor_value *monitor_value,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			int instance) {
	assert(monitor_value->type == MONITOR_VALUE_T__VALUE);

	struct printbuf * printbuf = printbuf_new();
	if(likely(NULL!=printbuf)) {
		const int sensor_id = rb_sensor_id(sensor);
		const char *sensor_name = rb_sensor_name(sensor);
		const char *monitor_group_name = rb_monitor_group_name(monitor);
		const char *monitor_type = rb_monitor_type(monitor);
		const char *monitor_unit = rb_monitor_unit(monitor);
		const char *monitor_instance_prefix =
					rb_monitor_instance_prefix(monitor);
		const char *monitor_name_split_suffix =
					rb_monitor_name_split_suffix(monitor);
		struct json_object *sensor_enrichment =
						rb_sensor_enrichment(sensor);
		// @TODO use printbuf_memappend_fast instead! */
		sprintbuf(printbuf, "{");
		sprintbuf(printbuf, "\"timestamp\":%lu",
						monitor_value->value.timestamp);
		if (sensor_id) {
			sprintbuf(printbuf, ",\"sensor_id\":%lu", sensor_id);
		}
		if (sensor_name) {
			sprintbuf(printbuf, ",\"sensor_name\":\"%s\"",
						sensor_name);
		}

		if (NO_INSTANCE != instance && monitor_name_split_suffix) {
			sprintbuf(printbuf, ",\"monitor\":\"%s%s\"",
						monitor_value->name,
						monitor_name_split_suffix);
		} else {
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",
							monitor_value->name);
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
		if (monitor_type) {
			sprintbuf(printbuf, ",\"type\":\"%s\"",monitor_type);
		}
		if (monitor_unit) {
			sprintbuf(printbuf, ",\"unit\":\"%s\"", monitor_unit);
		}
		if (monitor_group_name){
			sprintbuf(printbuf, ",\"group_name\":\"%s\"",
							monitor_group_name);
		}
		if (monitor_value->group_id){
			sprintbuf(printbuf, ",\"group_id\":%s",
						monitor_value->group_id);
		}
		if (sensor_enrichment) {
			print_monitor_value_enrichment(printbuf,
							sensor_enrichment);
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
			sensor, NO_INSTANCE);
	} else {
		size_t i_msgs = 0;
		assert(monitor_value->type == MONITOR_VALUE_T__ARRAY);
		for (size_t i=0; i<monitor_value->array.children_count; ++i) {
			if (monitor_value->array.children[i]) {
				print_monitor_value0(&ret->msgs[i_msgs++],
					monitor_value->array.children[i],
					monitor, sensor, i);
			}
		}

		if (monitor_value->array.split_op_result) {
			rb_message *msg = &ret->msgs[i_msgs++];
			assert(NULL == msg->payload);
			print_monitor_value0(msg,
				monitor_value->array.split_op_result, monitor,
				sensor, NO_INSTANCE);
		}

		ret->count = i_msgs;
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
