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

#include "config.h"

#include "rb_sensor.h"

#include "rb_json.h"

#include "rb_sensor_monitor_array.h"

#include <librd/rd.h>
#include <librd/rdlog.h>
#include <librd/rdfloat.h>

/// Sensor data
typedef struct {
	struct snmp_params_s snmp_params; ///< SNMP parameters
	json_object *enrichment; ///< Enrichment to use in monitors
	char *sensor_name;       ///< Sensor name to append in monitors
	uint64_t sensor_id;      ///< Sensor id to append in monitors
} sensor_data_t;

/// Sensor to monitor
struct rb_sensor_s {
#ifndef NDEBUG
#define RB_SENSOR_MAGIC 0xB30A1CB30A1CL
	uint64_t magic;
#endif

	sensor_data_t data;              ///< Data of sensor
	rb_monitors_array_t *monitors;   ///< Monitors to ask for
	rb_monitor_value_array_t *last_vals; ///< Last values
	ssize_t **op_vars; ///< Operation variables that needs each monitor
	int refcnt;                      ///< Reference counting
};

#ifdef RB_SENSOR_MAGIC
void assert_rb_sensor(rb_sensor_t *sensor) {
	assert(RB_SENSOR_MAGIC == sensor->magic);
}
#endif

const char *rb_sensor_name(const rb_sensor_t *sensor) {
	return sensor->data.sensor_name;
}

uint64_t rb_sensor_id(const rb_sensor_t *sensor) {
	return sensor->data.sensor_id;
}

struct json_object *rb_sensor_enrichment(const rb_sensor_t *sensor) {
	return sensor->data.enrichment;
}

/** Checks if a property is set. If not, it will show error message and will
  set aok to false
  @param ptr Pointer to check if a property is set.
  @param aok Return value.
  @param errmsg Error message
  @param sensor name Sensor name to give more information.
  @warning It will never set aok to true.
*/
static void check_setted(const void *ptr, bool *aok, const char *errmsg,
						const char *sensor_name) {
	assert(aok);
	assert(errmsg);

	if(*aok && ptr == NULL){
		*aok = 0;
		rdlog(LOG_ERR, "%s%s", errmsg,
				sensor_name ? sensor_name : "(some sensor)");
	}
}

/** Fill sensor information
  @param sensor Sensor to store information
  @param sensor_info JSON describing sensor
  */
static bool sensor_common_attrs_parse_json(rb_sensor_t *sensor,
					/* const */ json_object *sensor_info) {
	struct json_object *sensor_monitors = NULL;
	json_object_object_get_ex(sensor_info, "monitors", &sensor_monitors);
	if (NULL == sensor_monitors) {
		rdlog(LOG_ERR,
			"Could not obtain JSON sensors monitors. Skipping");
		return false;
	}

	sensor->data.snmp_params.session.timeout = PARSE_CJSON_CHILD_INT64(
			sensor_info, "timeout",
			(int64_t)sensor->data.snmp_params.session.timeout);
	sensor->data.sensor_id = PARSE_CJSON_CHILD_INT64(sensor_info,
			"sensor_id", 0);
	sensor->data.sensor_name = PARSE_CJSON_CHILD_STR(sensor_info,
			"sensor_name", NULL);
	sensor->data.snmp_params.peername = PARSE_CJSON_CHILD_STR(sensor_info,
			"sensor_ip", NULL);
	sensor->data.snmp_params.session.community = PARSE_CJSON_CHILD_STR(
						sensor_info, "community", NULL);

	json_object_object_get_ex(sensor_info, "enrichment",
						&sensor->data.enrichment);
	const char *snmp_version = PARSE_CJSON_CHILD_STR(sensor_info,
							"snmp_version", NULL);
	if (snmp_version) {
		sensor->data.snmp_params.session.version = net_snmp_version(
					snmp_version, sensor->data.sensor_name);
	}

	sensor->monitors = parse_rb_monitors(sensor_monitors);
	if (NULL != sensor->monitors) {
		const size_t monitors_count = sensor->monitors->count;
		sensor->op_vars = get_monitors_dependencies(sensor->monitors);
		sensor->last_vals = rb_monitor_value_array_new(monitors_count);
		if (NULL == sensor->last_vals) {
			rdlog(LOG_CRIT, "Couldn't allocate memory for sensor");
			return false;
		} else {
			sensor->last_vals->count = monitors_count;
		}
	}
	return NULL != sensor->monitors && NULL != sensor->last_vals;
}

/** Checks if a sensor is OK
  @param sensor_data Sensor to check
  @todo community and peername are not needed to check until we do SNMP stuffs
  */
static bool sensor_common_attrs_check_sensor(const rb_sensor_t *sensor) {
	bool aok = true;

	const char *sensor_name = sensor->data.sensor_name;

	check_setted(sensor->data.sensor_name,&aok,
		"[CONFIG] Sensor_name not setted in ", NULL);
	check_setted(sensor->data.snmp_params.peername,&aok,
		"[CONFIG] Peername not setted in sensor ", sensor_name);
	check_setted(sensor->data.snmp_params.session.community,&aok,
		"[CONFIG] Community not setted in sensor ", sensor_name);
	check_setted(sensor->monitors,&aok,
		"[CONFIG] Monitors not setted in sensor ", sensor_name);

	return aok;
}

/** Extract sensor common properties to all monitors
  @param sensor_data Return value
  @param sensor_info Original JSON to extract information
  @todo recorver sensor_info const (in modern cjson libraries)
  */
static bool sensor_common_attrs(rb_sensor_t *sensor,
					/* const */ json_object *sensor_info) {
	const bool rc = sensor_common_attrs_parse_json(sensor, sensor_info);
	return rc && sensor_common_attrs_check_sensor(sensor);
}

/** Sets sensor defaults
  @param worker_info Worker info that contains defaults
  @param sensor Sensor to store defaults
  */
static void sensor_set_defaults(const struct _worker_info *worker_info,
							rb_sensor_t *sensor) {
	sensor->data.snmp_params.session.timeout = worker_info->timeout;
	sensor->refcnt = 1;
}

/// @TODO make sensor_info const
rb_sensor_t *parse_rb_sensor(/* const */ json_object *sensor_info,
		const struct _worker_info *worker_info) {
	rb_sensor_t *ret = calloc(1,sizeof(*ret));

	if (ret) {
		sensor_set_defaults(worker_info, ret);
		const bool sensor_ok = sensor_common_attrs(ret, sensor_info);
		if (!sensor_ok) {
			rb_sensor_put(ret);
			ret = NULL;
		}
	}

	return ret;
}

/** Process a sensor
  @param worker_info Worker information needed to process sensor
  @param sensor Sensor
  @param ret Messages returned
  @return true if OK, false in other case
  */
bool process_rb_sensor(struct _worker_info *worker_info, rb_sensor_t *sensor,
							rb_message_list *ret) {
	return process_monitors_array(worker_info, sensor, sensor->monitors,
		sensor->last_vals, sensor->op_vars, &sensor->data.snmp_params,
		ret);
}

/// @todo find a better way
static void free_const_str(const char *str) {
	void *aux;
	memcpy(&aux,&str,sizeof(aux));
	free(aux);
}

/** Free allocated memory for sensor
  @param sensor Sensor to free
  */
static void sensor_done(rb_sensor_t *sensor) {
	free_const_str(sensor->data.snmp_params.peername);
	free_const_str(sensor->data.sensor_name);
	free_const_str(sensor->data.snmp_params.session.community);
	if (sensor->op_vars) {
		free_monitors_dependencies(sensor->op_vars,
						sensor->monitors->count);
	}
	if (sensor->monitors) {
		rb_monitors_array_done(sensor->monitors);
	}
	for (size_t i=0; sensor->last_vals && i<sensor->last_vals->count; ++i) {
		if (sensor->last_vals->elms[i]) {
			rb_monitor_value_done(sensor->last_vals->elms[i]);
		}
	}
	rb_monitor_value_array_done(sensor->last_vals);
	free(sensor);
}

void rb_sensor_get(rb_sensor_t *sensor) {
	ATOMIC_OP(add, fetch, &sensor->refcnt, 1);
}

void rb_sensor_put(rb_sensor_t *sensor) {
	if (0 == ATOMIC_OP(sub, fetch, &sensor->refcnt, 1)) {
		sensor_done(sensor);
	}
}
