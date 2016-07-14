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

#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <json-c/printbuf.h>

/* Copy just the 'useful' data of the node, not list-related */
void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src)
{
	assert(src);
	assert(dst);

#ifdef MONITOR_VALUE_MAGIC
	dst->magic = MONITOR_VALUE_MAGIC;
#endif
        dst->timestamp           = src->timestamp;
        if(src->name)
                dst->name            = rd_memctx_strdup(&dst->memctx,src->name);
        if(src->send_name)
                dst->send_name       = rd_memctx_strdup(&dst->memctx,src->send_name);
        if(src->instance_prefix)
                dst->instance_prefix = rd_memctx_strdup(&dst->memctx,src->instance_prefix);
        dst->instance            = src->instance;
        dst->instance_valid      = src->instance_valid;
        dst->bad_value           = src->bad_value;
        dst->value               = src->value;
        if(src->string_value)
                dst->string_value    = rd_memctx_strdup(&dst->memctx,src->string_value);
        if(src->group_id)
                dst->group_id        = rd_memctx_strdup(&dst->memctx,src->group_id);

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

struct printbuf *print_monitor_value(const struct monitor_value *monitor_value,
		const rb_monitor_t *monitor, const rb_sensor_t *sensor) {
	struct printbuf * printbuf = printbuf_new();
	if(likely(NULL!=printbuf)) {
		const int sensor_id = rb_sensor_id(sensor);
		const char *sensor_name = rb_sensor_name(sensor);
		const char *monitor_group_name = rb_monitor_group_name(monitor);
		const char *monitor_type = rb_monitor_type(monitor);
		const char *monitor_unit = rb_monitor_unit(monitor);
		struct json_object *sensor_enrichment =
						rb_sensor_enrichment(sensor);
		// @TODO use printbuf_memappend_fast instead! */
		sprintbuf(printbuf, "{");
		sprintbuf(printbuf, "\"timestamp\":%lu",
						monitor_value->timestamp);
		if (sensor_id) {
			sprintbuf(printbuf, ",\"sensor_id\":%lu", sensor_id);
		}
		if (sensor_name) {
			sprintbuf(printbuf, ",\"sensor_name\":\"%s\"",
						sensor_name);
		}
		if (monitor_value->send_name) {
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",
						monitor_value->send_name);
		} else {
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",
						monitor_value->name);
		}
		if (monitor_value->instance_valid &&
					monitor_value->instance_prefix) {
			sprintbuf(printbuf, ",\"instance\":\"%s%u\"",
						monitor_value->instance_prefix,
						monitor_value->instance);
		}
		if (rb_monitor_is_integer(monitor)) {
			sprintbuf(printbuf, ",\"value\":%"PRId64,
						(int64_t)monitor_value->value);
		} else {
			sprintbuf(printbuf, ",\"value\":\"%lf\"",
							monitor_value->value);
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
	}

	return printbuf;
}

void rb_monitor_value_done(struct monitor_value *mv) {
	rd_memctx_freeall(&mv->memctx);
	rd_memctx_destroy(&mv->memctx);
	free(mv);
}
