// rb_value.c

#include "rb_value.h"
#include "rb_log.h"

#include "librd/rdmem.h"
#include <json/printbuf.h>

/* Copy just the 'useful' data of the node, not list-related */
void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src)
{
	assert(src);
	assert(dst);
	dst->timestamp           = src->timestamp;
	dst->sensor_id           = src->sensor_id;
	dst->sensor_id_valid     = src->sensor_id_valid;
	if(src->sensor_name)
		dst->sensor_name     = rd_memctx_strdup(&dst->memctx,src->sensor_name);
	if(src->name)
		dst->name            = rd_memctx_strdup(&dst->memctx,src->name);
	if(src->send_name)
		dst->send_name       = rd_memctx_strdup(&dst->memctx,src->send_name);
	if(src->instance_prefix)
		dst->instance_prefix = rd_memctx_strdup(&dst->memctx,src->instance_prefix);
	dst->type                = src->type;
	dst->instance            = src->instance;
	dst->instance_valid      = src->instance_valid;
	dst->bad_value           = src->bad_value;
	dst->value               = src->value;
	dst->integer             = src->integer;
	if(src->string_value) 
		dst->string_value    = rd_memctx_strdup(&dst->memctx,src->string_value);
	if(src->unit) 
		dst->unit            = rd_memctx_strdup(&dst->memctx,src->unit);
	if(src->group_name) 
		dst->group_name      = rd_memctx_strdup(&dst->memctx,src->group_name);
	if(src->group_id) 
		dst->group_id        = rd_memctx_strdup(&dst->memctx,src->group_id);
	dst->enrichment          = src->enrichment;
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
					Log(LOG_ERR,"Cannot extract string value of enrichment key %s\n",key);
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
					Log(LOG_ERR,"Cannot extract int value of enrichment key %s\n",key);
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
				Log(LOG_ERR,"Can't enrich with objects/array at this time\n");
				break;
			}
		};
	}
}

struct printbuf * print_monitor_value(const struct monitor_value *monitor_value)
{
	struct printbuf * printbuf = printbuf_new();
	if(likely(NULL!=printbuf))
	{
		// @TODO use printbuf_memappend_fast instead! */
		sprintbuf(printbuf, "{");
		sprintbuf(printbuf,"\"timestamp\":%lu",monitor_value->timestamp);
		if(monitor_value->sensor_id_valid)
			sprintbuf(printbuf, ",\"sensor_id\":%lu",monitor_value->sensor_id);
		if(monitor_value->sensor_name)
			sprintbuf(printbuf, ",\"sensor_name\":\"%s\"",monitor_value->sensor_name);
		if(monitor_value->send_name)
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",monitor_value->send_name);
		else
			sprintbuf(printbuf, ",\"monitor\":\"%s\"",monitor_value->name);
		if(monitor_value->instance_valid && monitor_value->instance_prefix)
			sprintbuf(printbuf, ",\"instance\":\"%s%u\"",monitor_value->instance_prefix,monitor_value->instance);
		if(monitor_value->integer)
			sprintbuf(printbuf, ",\"value\":%ld", (long int)monitor_value->value);
		else
			sprintbuf(printbuf, ",\"value\":\"%lf\"", monitor_value->value);
		if(monitor_value->type)
			sprintbuf(printbuf, ",\"type\":\"%s\"",monitor_value->type());
		if(monitor_value->unit)
			sprintbuf(printbuf, ",\"unit\":\"%s\"", monitor_value->unit);
		if(monitor_value->group_name) 
			sprintbuf(printbuf, ",\"group_name\":\"%s\"", monitor_value->group_name);
		if(monitor_value->group_id)   
			sprintbuf(printbuf, ",\"group_id\":%s", monitor_value->group_id);
		if(monitor_value->enrichment)
			print_monitor_value_enrichment(printbuf,monitor_value->enrichment);
		sprintbuf(printbuf, "}");
	}

	return printbuf;
}