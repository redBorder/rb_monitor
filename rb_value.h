// rb_value.h

#pragma once

#include "rb_sensor_data.h"

#include "librd/rdlru.h"
#include "librd/rdavl.h"
#include "librd/rdmem.h"

#include <stdbool.h>

#define MONITOR_VALUE_MAGIC 0x12345678

struct monitor_value; /* FW declaration */

/** Function to get the network/system response.
 @param mv monitor value to save response to
 @param session network tecnology session (SNMP session, telnet session...)
 @param request request (oid, telnet command...)
 @return true if succeed. False ioc.
 */
typedef bool (*get_response_fn_t)(struct monitor_value *mv,void *session, const void *request);

/// @todo make the vectors entry here.
/// @note if you edit this structure, remember to edit monitor_value_copy
struct monitor_value{
	rd_memctx_t memctx;
	rd_avl_node_t avl_node;

	#ifdef MONITOR_VALUE_MAGIC
	int magic; // Private data, don't need to use them outside.
	#endif

	/* config.json extracted */
	bool sensor_id_valid;
	int sensor_id;
	const char * sensor_name;
	const char * name;            // Intern name: *__gid__*__pos__
	const char * send_name;       // Extern name. If not __gid__ nor __pos__, it is NULL and you have to check name.
	                              // @todo make a function name() for do the last.
	const char * instance_prefix;
	const char * name_split_suffix;
	const char * unit;
	const char * group_name;
	const char * group_id;
	const char * splittok;
	const char * splitop;
	bool nonzero;
	bool integer;
	bool timestamp_given;
	bool kafka;
	

	/* response */
	get_response_fn_t get_response_fn;
	time_t timestamp;
	double value;
	char * string_value;
	const char * (*type)(void); // way that the value has been obtained

	/* vector response */
	unsigned int instance;
	bool instance_valid;
	bool bad_value;
};

static void set_sensor_information(struct monitor_value *this,const struct _sensor_data *sensor_data)
{
	this->sensor_name = sensor_data->sensor_name;
	this->sensor_id = sensor_data->sensor_id;
	this->sensor_id_valid = sensor_data->sensor_id_valid;
}

void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src);

int process_monitor_value(struct monitor_value *monitor_value);

struct printbuf * print_monitor_value(const struct monitor_value *monitor_value);
