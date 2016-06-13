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

#include "rb_array.h"
#include "rb_snmp.h"

#include <librd/rdlru.h>
#include <librdkafka/rdkafka.h>
#include <json/json.h>
#include <stdbool.h>

/// SHARED Info needed by threads.
struct _worker_info{
	struct snmp_session default_session;
	pthread_mutex_t snmp_session_mutex;
	const char * community,*kafka_broker,*kafka_topic;
	const char * max_kafka_fails; /* I want a const char * because rd_kafka_conf_set implementation */

#ifdef HAVE_RBHTTP
	const char * http_endpoint;
	struct rb_http_handler_s *http_handler;
	pthread_t pthread_report;
#endif

	rd_kafka_t * rk;
	rd_kafka_topic_t * rkt;
	rd_kafka_conf_t * rk_conf;
	rd_kafka_topic_conf_t * rkt_conf;
	int64_t sleep_worker,max_snmp_fails,timeout,debug_output_flags;
	int64_t kafka_timeout;
	rd_fifoq_t *queue;
	struct monitor_values_tree * monitor_values_tree;
#ifdef HAVE_RBHTTP
	int64_t http_mode;
	int64_t http_insecure;
#endif
	int64_t http_max_total_connections;
	int64_t http_timeout;
	int64_t http_connttimeout;
	int64_t http_verbose;
	int64_t rb_http_max_messages;
};

typedef struct rb_sensor_s rb_sensor_t;

#ifdef NDEBUG
#define assert_rb_sensor(rb_sensor)
#else
void assert_rb_sensor(rb_sensor_t *sensor);
#endif

rb_sensor_t *parse_rb_sensor(/* const */ json_object *sensor_info,
		const struct _worker_info *worker_info);
bool process_rb_sensor(struct _worker_info *worker_info, rb_sensor_t *sensor,
								rd_lru_t *ret);

/** Obtains sensor name
  @param sensor Sensor
  @return Name of sensor.
  */
const char *rb_sensor_name(rb_sensor_t *sensor);

/** Obtains sensor id
  @param sensor Sensor
  @return Name of sensor.
  @todo this is not needed if we use proper enrichment
  */
uint64_t rb_sensor_id(rb_sensor_t *sensor);

/** Obtains sensor enrichment
  @param sensor Sensor
  @todo make const return
  */
struct json_object *rb_sensor_enrichment(rb_sensor_t *sensor);

/** Increase by 1 the reference counter for sensor
  @param sensor Sensor
  @todo this is not needed if we use proper enrichment
  */
void rb_sensor_get(rb_sensor_t *sensor);


/** Decrease the sensor reference counter.
  @param sensor Sensor
  */
void rb_sensor_put(rb_sensor_t *sensor);

/** Sensors array */
typedef struct rb_array rb_sensors_array_t;

/** Create a new array with count capacity */
#define rb_sensors_array_new(sz) rb_array_new(sz)

/** Destroy a sensors array */
#define rb_sensors_array_done(array) rb_array_done(array)

/** Checks if a sensor array is full */
#define rb_sensors_array_full(array) rb_array_full(array)

/** Add a sensor to sensors array
  @note Wrapper function allows typechecking */
static void rb_sensor_array_add(rb_sensors_array_t *array,
						rb_sensor_t *sensor) RD_UNUSED;
static void rb_sensor_array_add(rb_sensors_array_t *array,
						rb_sensor_t *sensor) {
	rb_array_add(array, sensor);
}

