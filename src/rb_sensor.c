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

static const char SENSOR_NAME_ENRICHMENT_KEY[] = "sensor_name";
static const char SENSOR_ID_ENRICHMENT_KEY[] = "sensor_id";

/// Sensor data
typedef struct {
	json_object *enrichment; ///< Enrichment to use in monitors
	struct snmp_params_s snmp_params; ///< SNMP parameters
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

/** We assume that sensor name is only requested in config errors, so we only
  save it in enrichment json
 * @param sensor Sensor to obtain string
 * @return Sensor name or "(some_sensor)" string if not defined
 */
const char *rb_sensor_name(const rb_sensor_t *sensor) {
	static const char failsafe_ret[] = "(some_sensor)";
	json_object *jsensor_name = NULL;
	const bool get_rc = json_object_object_get_ex(sensor->data.enrichment,
				SENSOR_NAME_ENRICHMENT_KEY, &jsensor_name);

	const char *ret = (get_rc && jsensor_name) ?
				json_object_get_string(jsensor_name) : NULL;
	return ret ? ret : failsafe_ret;
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
		rdlog(LOG_ERR, "%s%s", errmsg, sensor_name);

	}
}

/** Fill sensor information
  @param sensor Sensor to store information
  @param sensor_info JSON describing sensor
  */
static bool sensor_common_attrs_parse_json(rb_sensor_t *sensor,
					/* const */ json_object *sensor_info) {
	char errbuf[BUFSIZ];
	struct json_object *sensor_monitors = NULL;
	const int64_t sensor_id = PARSE_CJSON_CHILD_INT64(sensor_info,
						SENSOR_ID_ENRICHMENT_KEY, 0);
	char *sensor_name = PARSE_CJSON_CHILD_STR(sensor_info,
					SENSOR_NAME_ENRICHMENT_KEY, NULL);

	if (NULL == sensor_name) {
		rdlog(LOG_ERR, "Sensor with no name, couldn't parse");
		goto err;
	}

	json_object_object_get_ex(sensor_info, "monitors", &sensor_monitors);
	if (NULL == sensor_monitors) {
		rdlog(LOG_ERR,
			"Could not obtain JSON sensors monitors. Skipping");
		goto err;
	}

	sensor->data.snmp_params.session.timeout = PARSE_CJSON_CHILD_INT64(
			sensor_info, "timeout",
			(int64_t)sensor->data.snmp_params.session.timeout);
	sensor->data.snmp_params.peername = PARSE_CJSON_CHILD_STR(sensor_info,
			"sensor_ip", NULL);
	sensor->data.snmp_params.session.community = PARSE_CJSON_CHILD_STR(
						sensor_info, "community", NULL);

	const char *snmp_version = PARSE_CJSON_CHILD_STR(sensor_info,
							"snmp_version", NULL);
	if (snmp_version) {
		sensor->data.snmp_params.session.version = net_snmp_version(
					snmp_version, sensor_name);
	}

	json_object_object_get_ex(sensor_info, "enrichment",
						&sensor->data.enrichment);
	if ((sensor_name || sensor_id>0) && NULL == sensor->data.enrichment) {
		sensor->data.enrichment = json_object_new_object();
		if (NULL == sensor->data.enrichment) {
			rdlog(LOG_CRIT,
				"Couldn't allocate sensor %s enrichment",
				sensor_name);
			goto err;
		}
	}

	if (sensor_name) {
		const bool name_rc = ADD_JSON_STRING(sensor->data.enrichment,
					SENSOR_NAME_ENRICHMENT_KEY, sensor_name,
					errbuf, sizeof(errbuf));
		if (!name_rc) {
			rdlog(LOG_ERR,
				"Couldn't add sensor name to enrichment: %s",
				errbuf);
			goto err;
		}
		free(sensor_name);
		sensor_name = NULL;
	}

	if (sensor_id > 0) {
		const bool id_rc = ADD_JSON_INT64(sensor->data.enrichment,
					SENSOR_ID_ENRICHMENT_KEY, sensor_id,
					errbuf, sizeof(errbuf));
		if (!id_rc) {
			rdlog(LOG_ERR,
				"Couldn't add sensor id to enrichment: %s",
				errbuf);
			goto err;
		}
	}

	sensor->monitors = parse_rb_monitors(sensor_monitors,
						sensor->data.enrichment);
	if (NULL != sensor->monitors) {
		const size_t monitors_count = sensor->monitors->count;
		sensor->op_vars = get_monitors_dependencies(sensor->monitors);
		sensor->last_vals = rb_monitor_value_array_new(monitors_count);
		if (NULL == sensor->last_vals) {
			rdlog(LOG_CRIT, "Couldn't allocate memory for sensor");
			goto err;
		} else {
			sensor->last_vals->count = monitors_count;
		}
	} else {
		goto err;
	}

	return true;

err:
	free(sensor_name);
	return false;
}

/** Checks if a sensor is OK
  @param sensor_data Sensor to check
  @todo community and peername are not needed to check until we do SNMP stuffs
  */
static bool sensor_common_attrs_check_sensor(const rb_sensor_t *sensor) {
	bool aok = true;

	check_setted(sensor->data.snmp_params.peername,&aok,
				"[CONFIG] Peername not setted in a sensor",
							rb_sensor_name(sensor));
	check_setted(sensor->data.snmp_params.session.community,&aok,
				"[CONFIG] Community not setted in a sensor",
							rb_sensor_name(sensor));
	check_setted(sensor->monitors,&aok,
				"[CONFIG] Monitors not setted in a sensor",
							rb_sensor_name(sensor));

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
	if (sensor->data.enrichment) {
		json_object_put(sensor->data.enrichment);
	}
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
