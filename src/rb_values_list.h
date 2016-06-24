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

#pragma once

#include <stdbool.h>
#include <json/printbuf.h>

#include "rb_value.h"

struct monitor_values_tree;

struct monitor_values_tree * new_monitor_values_tree();

/** Add a monitor value to the tree.
  @param tree Tree to add monitor value
  @param mv Monitor value to add
  */
void add_monitor_value(struct monitor_values_tree *tree,
					struct monitor_value *mv);

/** Find requested monitor value
  @param tree Tree to search monitor value
  @param name Name of monitor value
  @param group_id Group id of the requested monitor value
  @return requested monitor value
  */
struct monitor_value *find_monitor_value(struct monitor_values_tree *tree,
					const char *name, const char *group_id);

/**
  Destroy values tree
 */
void destroy_monitor_values_tree(struct monitor_values_tree*tree);
