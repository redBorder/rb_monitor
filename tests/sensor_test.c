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

#include "sensor_test.h"

#include "rb_sensor.h"

void test_sensor_n(const char *cjson_sensor, check_list_t *checks,
                                                                size_t n) {
        struct _worker_info worker_info;
        memset(&worker_info, 0, sizeof(worker_info));

        snmp_sess_init(&worker_info.default_session);
        struct json_object *json_sensor = json_tokener_parse(cjson_sensor);
        rb_sensor_t *sensor = parse_rb_sensor(json_sensor, &worker_info);
        json_object_put(json_sensor);

        for (size_t i=0;i<n;++i) {
                rb_message_list messages;
                rb_message_list_init(&messages);
                process_rb_sensor(&worker_info, sensor, &messages);
                json_list_check(&checks[i], &messages);
        }
        rb_sensor_put(sensor);
}
