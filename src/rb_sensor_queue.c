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

#include <rb_sensor_queue.h>

void sensor_queue_init(sensor_queue_t *queue) {
	memset(queue,0,sizeof(*queue)); // Needed even with init()
	rd_fifoq_init(queue);
}

void sensor_queue_done(sensor_queue_t *queue) {
	rd_fifoq_destroy(queue);
}
