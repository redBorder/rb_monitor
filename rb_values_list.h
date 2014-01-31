// rb_values_list.h

#pragma once

#include "rb_value.h"

#include <stdbool.h>
#include "librd/rdlru.h"
#include "librd/rdavl.h"
#include <json/printbuf.h>


struct monitor_values_tree;

struct monitor_values_tree * new_monitor_values_tree();

/**
  Add a monitor value to a monitor_values_tree.
  @note src value will be copied.
 */
struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,struct monitor_value *src);

struct monitor_value * find_monitor_value(struct monitor_values_tree *tree,const struct monitor_value *node);

/**
  Destroy values tree
 */
void destroy_monitor_values_tree(struct monitor_values_tree*tree);