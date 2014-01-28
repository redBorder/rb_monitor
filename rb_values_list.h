
/*
** Copyright (C) 2014 Eneo Tecnologia S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

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
const struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,const struct monitor_value *src);

/**
  Destroy values tree
 */
void destroy_monitor_values_tree(struct monitor_values_tree*tree);