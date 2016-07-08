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

#include <json-c/json.h>

#define RB_SENSOR_MAGIC 0xB30A1CB30A1CL

struct rb_sensor {
#ifdef RB_SENSOR_MAGIC
  uint64_t magic;
#endif

  json_object *json_sensor;
#define RB_SENSOR_F_FREE 0x01
  int flags;
};
