/*
  Copyright (C) 2015 Eneo Tecnologia S.L.
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

#include "config.h"

#ifdef HAVE_ZOOKEEPER

#include <json/json.h>
#include <librd/rdqueue.h>

struct rb_monitor_zk;
struct rb_monitor_zk *init_rbmon_zk(char *host,uint64_t pop_watcher_timeout,
  uint64_t push_timeout,json_object *zk_sensors,rd_fifoq_t *workers_queue);

void stop_zk(struct rb_monitor_zk *zk);

#endif
