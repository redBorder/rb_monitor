// rb_values_list.h

#pragma once

#include <stdbool.h>
#include "librd/rdlru.h"
#include "librd/rdavl.h"
// #define MONITOR_VALUE_MAGIC 0x12345678

/// @todo make the vectors entry here.
/// @note if you edit this structure, remember to edit monitor_value_copy
struct monitor_value{
	#ifdef MONITOR_VALUE_MAGIC
	int magic; // Private data, don't need to use them outside.
	#endif

	time_t timestamp;
	int sensor_id;
	const char * sensor_name;
	const char * name;            // Intern name: *__gid__*__pos__
	const char * send_name;       // Extern name. If not __gid__ nor __pos__, it is NULL and you have to check name.
	                              // @todo make a function name() for do the last.
	const char * instance_prefix;
	unsigned int instance;
	bool instance_valid;
	double value;
	const char * string_value;
	bool bad_value;
	bool integer;
	const char * unit;
	const char * group_name;
	const char * group_id;
	rd_avl_node_t avl_node;
};

struct monitor_values_tree;

struct monitor_values_tree * new_monitor_values_tree();

/**
  Add a monitor value to a monitor_values_tree.
  @note src value will be copied.
 */
const struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,const struct monitor_value *src);

/**
  Destroy values tree
 */
void destroy_monitor_values_tree(struct monitor_values_tree*tree);