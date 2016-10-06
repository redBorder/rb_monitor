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

#include "rb_sensor.h"

#include <librd/rdqueue.h>
#include <librd/rdthread.h>

#include <assert.h>

/// Sensors queue
typedef rd_fifoq_t sensor_queue_t;

/** Initialize a new sensor queue
  @param queue Queue to init
  */
void sensor_queue_init(sensor_queue_t *queue);
/** Destroy a sensor queue
  @param queue Queue to finish
  */
void sensor_queue_done(sensor_queue_t *queue);

/** Queue a sensor
  @param queue Queue
  @param sensor Sensor
  */
#define queue_sensor rd_fifoq_add

/** Pop a sensor from the queue sensor
  @param queue Queue of sensors
  @tmo_ms Timeout in ms
  @return Sensor extracted, or NULL if any
  */
static rb_sensor_t *
pop_sensor(sensor_queue_t *queue, int tmo_ms) __attribute__((unused));
static rb_sensor_t *pop_sensor(sensor_queue_t *queue, int tmo_ms) {
	rb_sensor_t *sensor = NULL;
	rd_fifoq_elm_t *elm = rd_fifoq_pop_timedwait(queue, tmo_ms);
	if (elm) {
		sensor = elm->rfqe_ptr;
		rd_fifoq_elm_release(queue, elm);
		assert_rb_sensor(sensor);
	}

	return sensor;
}
