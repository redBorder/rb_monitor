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

#include <rb_snmp.h>

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

#define RB_SENSOR_MAGIC 0xB30A1CB30A1CL

struct rb_sensor {
#ifdef RB_SENSOR_MAGIC
	uint64_t magic;
#endif

	json_object *json_sensor;
#define RB_SENSOR_F_FREE 0x01
	int flags;
};

bool process_sensor(struct _worker_info *worker_info, json_object *sensor_info,
								rd_lru_t *ret);
