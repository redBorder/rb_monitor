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

#include "sensor_test.h"

#include "rb_sensor.h"

#include "rb_values_list.h"

void test_sensor(const char *cjson_sensor, check_list_t *checks) {
	struct _worker_info worker_info;
	rd_lru_t *messages = rd_lru_new();

	memset(&worker_info, 0, sizeof(worker_info));

	worker_info.monitor_values_tree = new_monitor_values_tree();
	snmp_sess_init(&worker_info.default_session);
	struct json_object *json_sensor = json_tokener_parse(cjson_sensor);

	process_sensor(&worker_info, json_sensor, messages);
	json_list_check(checks, messages);
	json_object_put(json_sensor);

	destroy_monitor_values_tree(worker_info.monitor_values_tree);
	rd_lru_destroy(messages);
}
