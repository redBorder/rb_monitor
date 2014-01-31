// rb_value.c

#include "rb_log.h"

#include "rb_value.h"

#include "librd/rdmem.h"
#include <json/printbuf.h>

/* Copy just the 'useful' data of the node, not list-related */
void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src)
{
	assert(src);
	assert(dst);

	#ifdef MONITOR_VALUE_MAGIC
	assert(src->magic == MONITOR_VALUE_MAGIC);
	//assert(dst->magic == MONITOR_VALUE_MAGIC);
	#endif

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
		sprintbuf(printbuf, "}");
	}

	return printbuf;
}

int process_monitor_value(struct monitor_value *monitor_value)
{
	if(monitor_value->string_value)
	{
		if(likely(strlen(monitor_value->string_value)!=0))
		{
			monitor_value->value = atof(monitor_value->string_value);
			return 1;
		}
		else
		{
			Log(LOG_WARNING,"Not seeing %s value.\n", monitor_value->name);
			return 0;
		}
	}
	else
	{
		Log(LOG_WARNING,"NULL value in %s\n",monitor_value->name);
		return 0;
	}
	return 0;
}

void set_json_information(struct monitor_value *monitor_value,json_object *attributes_array)
{
	monitor_value->kafka = 1; // default
	json_object_object_foreach(attributes_array,key,val)
	{
		errno=0;
		if(0==strncmp(key,"split",strlen("split")+1)){
			monitor_value->splittok = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"split_op",strlen("split_op"))){
			monitor_value->splitop = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"name",strlen("name")+1)){ 
			monitor_value->name = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"name_split_suffix",strlen("name_split_suffix"))){
			monitor_value->name_split_suffix = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strcmp(key,"instance_prefix")){
			monitor_value->instance_prefix = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"unit",strlen("unit"))){
			monitor_value->unit = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"group_name",strlen("group_name"))){
			monitor_value->group_name = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strncmp(key,"group_id",strlen("group_id"))){
			monitor_value->group_id = rd_memctx_strdup(&monitor_value->memctx,json_object_get_string(val));
		}else if(0==strcmp(key,"nonzero")){
			monitor_value->nonzero = 1;
		}else if(0==strcmp(key,"timestamp_given")){
			monitor_value->timestamp_given=json_object_get_int64(val);
		}else if(0==strncmp(key,"kafka",strlen("kafka")) || 0==strncmp(key,"name",strlen("name"))){
			monitor_value->kafka = json_object_get_int64(val);
		}else if(0==strcmp(key,"integer")){
			monitor_value->integer = json_object_get_int64(val);
		}else if(0==strncmp(key,"oid",strlen("oid")) || 0==strncmp(key,"op",strlen("op"))){
			// not to be processed here
		}else if(0==strncmp(key,"system",strlen("system"))){
			// not to be processed here
		}else{
			Log(LOG_ERR,"Cannot parse %s argument\n",key);
		}
		if(errno!=0){
			Log(LOG_ERR,"Could not parse %s value: %s\n",key,strerror(errno));
		}

	} /* foreach */
}
