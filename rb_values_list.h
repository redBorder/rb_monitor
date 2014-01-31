// rb_values_list.h

#pragma once

#include <stdbool.h>
#include "librd/rdlru.h"
#include "librd/rdavl.h"
#include <json/printbuf.h>

#include "rb_value.h"

struct monitor_values_tree;

struct monitor_values_tree * new_monitor_values_tree();

/**
  Add a monitor value to a monitor_values_tree.
  @note src value will be copied.
 */
struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,struct monitor_value *src);

/**
  Destroy values tree
 */
void destroy_monitor_values_tree(struct monitor_values_tree*tree);