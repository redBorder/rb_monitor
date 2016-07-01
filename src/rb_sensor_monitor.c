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

#include "rb_sensor_monitor.h"

#include "rb_sensor.h"

#include "rb_snmp.h"
#include "rb_system.h"
#include "rb_libmatheval.h"

#include "rb_json.h"

#include <matheval.h>
#include <librd/rdfloat.h>

static const char DEFAULT_TIMESTAMP_SEP[] = ":";

#ifndef NDEBUG
#define RB_MONITOR_MAGIC 0x0b010a1c0b010a1cl
#endif

/// X-macro to define monitor operations
/// _X(menum,cmd,value_type,fn)
#define MONITOR_CMDS_X \
	/* Will launch a shell and execute given command, capturing output */  \
	_X(RB_MONITOR_T__SYSTEM,"system","system",                             \
		rb_monitor_get_system_external_value)                          \
	/* Will ask SNMP server for a given oid */                             \
	_X(RB_MONITOR_T__OID,"oid","snmp",                                     \
		rb_monitor_get_snmp_external_value)                            \
	/* Will operate over previous results */                               \
	_X(RB_MONITOR_T__OP,"op","op",                                         \
		rb_monitor_get_op_result)

struct rb_monitor_s {
#ifdef RB_MONITOR_MAGIC
	uint64_t magic;
#endif
	enum monitor_cmd_type {
#define _X(menum,cmd,type,fn) menum,
		MONITOR_CMDS_X
#undef _X
	} type;
	const char *name;     ///< Name of monitor
	const char *argument; ///< Given argument to command / oid / op
	/// If the monitor is a vector response, how to name each sub-monitor
	const char *name_split_suffix;
	/** If the monitor is a vector response, each one will have a "instance"
	  key with value instance-%d */
	const char *instance_prefix;
	const char *group_id;         ///< Sensor group id
	const char *group_name;       ///< Sensor group name
	uint64_t send; ///< Send the monitor to output or not
	uint64_t timestamp_given; ///< Timestamp is given in response
	uint64_t integer; ///< Response must be an integer
	const char *splittok; ///< How to split response
	const char *splitop; ///< Do a final operation with tokens
	const char *unit; ///< Monitor unit
	const char *cmd_arg; ///< Argument given to command
};

#ifndef NDEBUG
void assert_rb_monitor(const rb_monitor_t *monitor) {
	assert(RB_MONITOR_MAGIC == (monitor)->magic);
}
#endif

const char *rb_monitor_type(const rb_monitor_t *monitor) {
	assert(monitor);

	switch(monitor->type) {
#define _X(menum,cmd,type,fn) case menum: return type;
		MONITOR_CMDS_X
#undef _X
	default: return NULL;
	};
}

const char *rb_monitor_name(const rb_monitor_t *monitor) {
	return monitor->name;
}

const char *rb_monitor_instance_prefix(const rb_monitor_t *monitor) {
	return monitor->instance_prefix;
}

bool rb_monitor_timestamp_provided(const rb_monitor_t *monitor) {
	return monitor->timestamp_given;
}

const char *rb_monitor_name_split_suffix(const rb_monitor_t *monitor) {
	return monitor->name_split_suffix;
}

const char *rb_monitor_group_id(const rb_monitor_t *monitor) {
	return monitor->group_id;
}

const char *rb_monitor_group_name(const rb_monitor_t *monitor) {
	return monitor->group_name;
}

bool rb_monitor_is_integer(const rb_monitor_t *monitor) {
	return monitor->integer;
}

const char *rb_monitor_unit(const rb_monitor_t *monitor) {
	return monitor->unit;
}

bool rb_monitor_send(const rb_monitor_t *monitor) {
	return monitor->send;
}

const char *rb_monitor_get_cmd_data(const rb_monitor_t *monitor) {
	return monitor->argument;
}

void rb_monitor_get_op_variables(const rb_monitor_t *monitor,char ***vars,
							size_t *vars_size) {
	struct {
		char **vars;
		int count;
	} all_vars;

	if (monitor->type != RB_MONITOR_T__OP) {
		goto no_deps;
	}

	void *const evaluator = evaluator_create((char *)monitor->cmd_arg);
	if (NULL == evaluator) {
		rdlog(LOG_ERR, "Couldn't create an evaluator from %s",
			monitor->cmd_arg);
		goto no_deps;
	}

	evaluator_get_variables(evaluator, &all_vars.vars, &all_vars.count);
	(*vars) = malloc(all_vars.count*sizeof((*vars)[0]));
	for (int i=0; vars && i<all_vars.count; ++i) {
		(*vars)[i] = strdup(all_vars.vars[i]);
		if (NULL == (*vars)[i]) {
			rdlog(LOG_ERR, "Couldn't strdup (OOM?)");
			rb_monitor_free_op_variables(*vars, i);
			evaluator_destroy(evaluator);
			goto no_deps;
		}
	}
	evaluator_destroy(evaluator);
	*vars_size = all_vars.count;
	return;

no_deps:
	*vars = NULL;
	*vars_size = 0;
}

void rb_monitor_free_op_variables(char **vars, size_t vars_size) {
	for (size_t i=0; i<vars_size; ++i) {
		free(vars[i]);
	}
	free(vars);
}

/** Free a const string.
  @todo remove this, is ugly
  */
static void free_const_str(const char *str) {
	char *aux;
	memcpy(&aux, &str, sizeof(aux));
	free(aux);
}

void rb_monitor_done(rb_monitor_t *monitor) {
	free_const_str(monitor->name);
	free_const_str(monitor->argument);
	free_const_str(monitor->name_split_suffix);
	free_const_str(monitor->instance_prefix);
	free_const_str(monitor->group_id);
	free_const_str(monitor->group_name);
	free_const_str(monitor->splittok);
	free_const_str(monitor->splitop);
	free_const_str(monitor->unit);
	free_const_str(monitor->cmd_arg);
	free(monitor);
}

/** strtod conversion plus set errno=EINVAL if no conversion is possible
  @param str input string
  @return double
  */
static double toDouble(const char *str) {
	char *endPtr;
	errno=0;
	double d = strtod(str,&endPtr);
	return d;
}

/** Get monitor command
  @param monitor Monitor to save command
  @param json_monitor JSON monitor to extract command
  */
static bool extract_monitor_cmd(enum monitor_cmd_type *type,
			const char **cmd_arg, struct json_object *json_monitor) {
	static const char *cmd_keys[] = {
#define _X(menum,cmd,type,fn) cmd,
	MONITOR_CMDS_X
#undef _X
	};

	for (size_t i=0; i<RD_ARRAYSIZE(cmd_keys); ++i) {
		struct json_object *json_cmd_arg = NULL;
		const bool get_rc = json_object_object_get_ex(json_monitor,
						cmd_keys[i], &json_cmd_arg);
		if (get_rc) {
			*type = i;
			*cmd_arg = json_object_get_string(json_cmd_arg);
			return true;
		}
	}

	return false;
}

/** Checks if a split operation is valid
  @param split_op requested split op
  @return true if valid, valse in other case
  */
static bool valid_split_op(const char *split_op) {
	const char *ops[] = {"sum", "mean"};
	for (size_t i=0; i<RD_ARRAYSIZE(ops); ++i) {
		if (0==strcmp(ops[i], split_op)) {
			return true;
		}
	}
	return false;
}

/** Parse a JSON monitor
  @param type Type of monitor (oid, system, op...)
  @param cmd_arg Argument of monitor (desired oid, system command, operation...)
  @return New monitor
  */
static rb_monitor_t *parse_rb_monitor0(enum monitor_cmd_type type,
			const char *cmd_arg, struct json_object *json_monitor) {
	char *aux_name = PARSE_CJSON_CHILD_STR(json_monitor,
							"name", NULL);
	char *aux_split_op = PARSE_CJSON_CHILD_STR(json_monitor,
							"split_op", NULL);

	int aux_timestamp_given = PARSE_CJSON_CHILD_INT64(json_monitor,
							"timestamp_given", 0);

	if (NULL == aux_name) {
		rdlog(LOG_ERR, "Monitor with no name");
		return NULL;
	}

	if (aux_split_op && !valid_split_op(aux_split_op)) {
		rdlog(LOG_WARNING, "Invalid split op %s of monitor %s",
			aux_split_op, aux_name);
		free(aux_split_op);
		aux_split_op = NULL;
	}

	if (type == RB_MONITOR_T__OP && aux_timestamp_given) {
		rdlog(LOG_WARNING, "Can't provide timestamp in op monitor (%s)",
			aux_name);
		aux_timestamp_given = 0;
	}

	/// tmp monitor to locate all string parameters
	rb_monitor_t *ret = calloc(1, sizeof(*ret));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Can't alloc sensor monitor (out of memory?)");
		return NULL;
	}

#ifdef RB_MONITOR_MAGIC
	ret->magic = RB_MONITOR_MAGIC;
#endif

	ret->splittok = PARSE_CJSON_CHILD_STR(json_monitor, "split", NULL);
	ret->splitop = aux_split_op;
	ret->name = aux_name;
	ret->name_split_suffix = PARSE_CJSON_CHILD_STR(json_monitor,
						"name_split_suffix", NULL);
	ret->instance_prefix = PARSE_CJSON_CHILD_STR(json_monitor,
						"instance_prefix", NULL);
	ret->unit = PARSE_CJSON_CHILD_STR(json_monitor, "unit", NULL);
	ret->group_name = PARSE_CJSON_CHILD_STR(json_monitor,
							"group_name", NULL);
	ret->group_id = PARSE_CJSON_CHILD_STR(json_monitor, "group_id", NULL);
	ret->timestamp_given = aux_timestamp_given;
	ret->send = PARSE_CJSON_CHILD_INT64(json_monitor, "send", 1);
	ret->integer = PARSE_CJSON_CHILD_INT64(json_monitor, "integer", 0);
	ret->type = type;
	ret->cmd_arg = strdup(cmd_arg);

	return ret;
}

rb_monitor_t *parse_rb_monitor(struct json_object *json_monitor) {
	enum monitor_cmd_type cmd_type;
	const char *cmd_arg = NULL;

	const bool extract_cmd_rc = extract_monitor_cmd(&cmd_type, &cmd_arg,
								json_monitor);
	if (!extract_cmd_rc){
		rdlog(LOG_ERR, "Couldn't extract monitor command");
		return NULL;
	} else if(NULL == cmd_arg) {
		rdlog(LOG_ERR, "Couldn't extract monitor command value");
		return NULL;
	} else {
		rb_monitor_t *ret = parse_rb_monitor0(cmd_type, cmd_arg,
								json_monitor);

		return ret;
	}

	return NULL;
}

/** Context of sensor monitors processing */
struct process_sensor_monitor_ctx {
	struct monitor_snmp_session *snmp_sessp; ///< Base SNMP session
};

struct process_sensor_monitor_ctx *new_process_sensor_monitor_ctx(
				size_t monitors_count,
				struct monitor_snmp_session *snmp_sessp) {
	struct process_sensor_monitor_ctx *ret = calloc(1, sizeof(*ret));
	if (NULL == ret) {
		rdlog(LOG_ERR, "Couldn't allocate process sensor monitors ctx");
	} else {
		ret->snmp_sessp = snmp_sessp;
	}

	return ret;
}

void destroy_process_sensor_monitor_ctx(
				struct process_sensor_monitor_ctx *ctx) {
	free(ctx);
}

/* FW declaration */
static struct monitor_value *process_novector_monitor(
			const rb_monitor_t *monitor, const char *value_buf,
			double number, time_t now);

static struct monitor_value *process_vector_monitor(
			const rb_monitor_t *monitor, const char *value_buf,
			double number, time_t now);

/** Base function to obtain an external value, and to manage it as a vector or
  as an integer
  @param process_ctx Processing context
  @param monitor Monitor to process
  @param send Send value to kafka
  @todo delete send argument, just do not add to valueslist vector!
  @param get_value_cb Callback to get value
  @param get_value_cb_ctx Context send to get_value_cb
  @return Monitor values array
  */
static struct monitor_value *rb_monitor_get_external_value(
		struct process_sensor_monitor_ctx *process_ctx,
		const rb_monitor_t *monitor,
		bool (*get_value_cb)(char *buf, size_t bufsiz, double *number,
						void *ctx, const char *arg),
		void *get_value_cb_ctx) {
	double number = 0;
	char value_buf[BUFSIZ];
	value_buf[0] = '\0';
	struct monitor_value *ret = NULL;
	const bool ok = get_value_cb(value_buf, sizeof(value_buf), &number,
					get_value_cb_ctx, monitor->cmd_arg);

	if(0 == strlen(value_buf)) {
		rdlog(LOG_WARNING,"Not seeing %s value.", monitor->name);
		return false;
	}

	if(!monitor->splittok) {
		if (!ok) {
			rdlog(LOG_WARNING,
				"Value '%s' of [%s:%s] is not a number",
						value_buf, monitor->cmd_arg,
						monitor->name);
			return false;
		}
		ret = process_novector_monitor(monitor, value_buf, number,
								time(NULL));
	} else /* We have a vector here */ {
		/** @todo delete parameters that could be sent
			using monitor */
		ret = process_vector_monitor(monitor, value_buf, number,
								time(NULL));
	}

	return ret;
}

/** Convenience function to obtain system values */
static struct monitor_value *rb_monitor_get_system_external_value(
				const rb_monitor_t *monitor,
				struct process_sensor_monitor_ctx *process_ctx,
				rb_monitor_value_array_t *ops_vars) {
	return rb_monitor_get_external_value(process_ctx, monitor,
						system_solve_response, NULL);
}

/** Convenience function */
static bool snmp_solve_response0(char *value_buf, size_t value_buf_len,
			double *number, void *session, const char *oid_string) {
	return snmp_solve_response(value_buf, value_buf_len, number,
		(struct monitor_snmp_session *)session, oid_string);
}

/** Convenience function to obtain SNMP values */
static struct monitor_value *rb_monitor_get_snmp_external_value(
				const rb_monitor_t *monitor,
				struct process_sensor_monitor_ctx *process_ctx,
				rb_monitor_value_array_t *op_vars) {
	return rb_monitor_get_external_value(process_ctx, monitor,
				snmp_solve_response0, process_ctx->snmp_sessp);
}

/** Create a libmatheval vars using op_vars */
static struct libmatheval_vars *op_libmatheval_vars(
					rb_monitor_value_array_t *op_vars) {
	struct libmatheval_vars *libmatheval_vars = new_libmatheval_vars(
								op_vars->count);
	size_t expected_v_elms = 0;
	unsigned int expected_mv_type = 0;
	if (NULL == libmatheval_vars) {
		/// @todo error treatment
		return NULL;
	}

	for (size_t i=0; i<op_vars->count; ++i) {
		struct monitor_value *mv = rb_monitor_value_array_at(op_vars,
									i);

		if (mv) {
			libmatheval_vars->names[i] = (char *)mv->name;

			if (0==libmatheval_vars->count) {
				expected_mv_type = mv->type;
				if (MONITOR_VALUE_T__ARRAY == mv->type) {
					expected_v_elms =
						mv->array.children_count;
				}
			} else if (expected_mv_type != mv->type) {
				rdlog(LOG_ERR,
					"trying to operate with vector and array");
				goto err;
			} else if (mv->type == MONITOR_VALUE_T__ARRAY
					&& mv->array.children_count
							!= expected_v_elms) {
				rdlog(LOG_ERR,
					"trying to operate on vectors of different size:"
					"[(previous size):%zu] != [%s:%zu]",
					expected_v_elms, mv->name,
					mv->array.children_count);
				goto err;
			}

			libmatheval_vars->count++;
		}
	}

	return libmatheval_vars;
err:
	delete_libmatheval_vars(libmatheval_vars);
	return NULL;
}

/** Do a monitor operation
  @param f evaluator
  @param libmatheval_vars prepared libmathevals with names and values
  @param monitor Monitor operation belongs
  @param now This time
  @return New monitor value
  */
static struct monitor_value *rb_monitor_op_value0(void *f,
			struct libmatheval_vars *libmatheval_vars,
			const rb_monitor_t *monitor, const time_t now) {

	const char *operation = monitor->cmd_arg;
	const double number = evaluator_evaluate (f, libmatheval_vars->count,
			libmatheval_vars->names,
			libmatheval_vars->values);

	rdlog(LOG_DEBUG, "Result of operation [%s]: %lf", monitor->cmd_arg,
									number);

	if(!isnormal(number)) {
		rdlog(LOG_ERR, "OP %s return a bad value: %lf. Skipping.",
			operation, number);
		return NULL;
	}

	char val_buf[64];
	sprintf(val_buf,"%lf",number);

	return process_novector_monitor(monitor, val_buf, number, now);
}

/** Do a monitor value operation, with no array involved
  @param f Evaluator
  @param op_vars Operations variables with names
  @param monitor Montior this operation belongs
  @param now This time
  @return new monitor value with operation result
  */
static struct monitor_value *rb_monitor_op_value(void *f,
			rb_monitor_value_array_t *op_vars,
			struct libmatheval_vars *libmatheval_vars,
			const rb_monitor_t *monitor, const time_t now) {

	/* Foreach variable in operation, value */
	for (size_t v=0; v<op_vars->count; ++v) {
		const struct monitor_value *mv_v =
				rb_monitor_value_array_at(op_vars, v);
		assert(mv_v);
		assert(MONITOR_VALUE_T__VALUE == mv_v->type);
		libmatheval_vars->values[v] = mv_v->value.value;
	}

	return rb_monitor_op_value0(f, libmatheval_vars, monitor, now);
}

/** Gets an operation result of vector position i
  @param f evaluator
  @param libmatheval_vars Libmatheval prepared variables
  @param v_pos Vector position we want to evaluate
  @param monitor Monitor this operation belongs
  @param now Time of operation
  @return Montior value with vector index i of the result
  @todo merge with rb_monitor_op_value
  */
static struct monitor_value *rb_monitor_op_vector_i(void *f,
			rb_monitor_value_array_t *op_vars,
			struct libmatheval_vars *libmatheval_vars,
			size_t v_pos,
			const rb_monitor_t *monitor, const time_t now) {
	/* Foreach variable in operation, use element i of vector */
	for (size_t v=0; v<op_vars->count; ++v) {
		const struct monitor_value *mv_v =
				rb_monitor_value_array_at(op_vars, v);
		assert(mv_v);
		assert(MONITOR_VALUE_T__ARRAY == mv_v->type);

		const struct monitor_value *mv_v_i =
						mv_v->array.children[v_pos];
		if (NULL == mv_v_i) {
			// We don't have this value, so we can't do operation
			return NULL;
		}

		assert(MONITOR_VALUE_T__VALUE == mv_v_i->type);
		assert(0==strcmp(mv_v_i->name,libmatheval_vars->names[v]));

		libmatheval_vars->values[v] = mv_v_i->value.value;
	}

	return rb_monitor_op_value0(f, libmatheval_vars, monitor, now);
}

/** Makes a vector operation
  @param f libmatheval evaluator
  @param op_vars Monitor values of operation variables
  @param libmatheval_vars Libmatheval variables template
  @param monitor Monitor this operation belongs
  @param now Operation's time
  @todo op_vars should be const
  */
static struct monitor_value *rb_monitor_op_vector(void *f,
				rb_monitor_value_array_t *op_vars,
				struct libmatheval_vars *libmatheval_vars,
				const rb_monitor_t *monitor, time_t now) {
	double sum = 0;
	size_t count = 0;
	const struct monitor_value *mv_0 = rb_monitor_value_array_at(op_vars,
									0);
	struct monitor_value *split_op = NULL;
	struct monitor_value **children = calloc(mv_0->array.children_count,
							sizeof(children[0]));
	if (NULL == children) {
		/* @todo Error treatment */
		rdlog(LOG_ERR,
			"Couldn't create monitor value %s"
			" children (out of memory?)", monitor->name);
		return NULL;
	}

	// Foreach member of vector
	for (size_t i=0;  i < mv_0->array.children_count; ++i) {
		children[i] = rb_monitor_op_vector_i(f, op_vars,
			libmatheval_vars, i, monitor, now);

		if (NULL != children[i]) {
			sum+=children[i]->value.value;
			count++;
		}
	} /* foreach member of vector */

	if(monitor->splitop && count > 0) {
		char string_value[64];

		const double split_op_value =
			0 == strcmp(monitor->splitop, "sum") ? sum : sum/count;

		/// @todo check if number is normal
		/// @todo check snprintf return
		snprintf(string_value, sizeof(string_value), "%lf",
								split_op_value);

		split_op = process_novector_monitor(monitor, string_value,
			split_op_value, now);
	}

	return new_monitor_value_array(monitor->name,
		mv_0->array.children_count, children, split_op);
}

/** Process an operation monitor
  @param monitor Monitor this operation belongs
  @param sensor Sensor this monitor belongs
  @param process_ctx Monitor process context
  @return New monitor value with operation result
  */
static struct monitor_value *rb_monitor_get_op_result(
			const rb_monitor_t *monitor,
			struct process_sensor_monitor_ctx *process_ctx,
			rb_monitor_value_array_t *op_vars) {
	struct monitor_value *ret = NULL;
	const char *operation = monitor->cmd_arg;

	/// @todo error treatment in this cases
	if (NULL == op_vars) {
		return NULL;
	} else if (0 == op_vars->count) {
		return NULL;
	}

	void *const f = evaluator_create((char *)operation);
	if (NULL == f) {
		rdlog(LOG_ERR, "Couldn't create evaluator (invalid op [%s]?",
			operation);
		return NULL;
	}

	const time_t now = time(NULL);
	struct libmatheval_vars *libmatheval_vars = op_libmatheval_vars(
								op_vars);
	if (NULL == libmatheval_vars) {
		goto libmatheval_error;
	}

	const struct monitor_value *mv_0 = rb_monitor_value_array_at(op_vars,
									0);
	if (mv_0) {
		switch(mv_0->type) {
		case MONITOR_VALUE_T__ARRAY:
			ret = rb_monitor_op_vector(f, op_vars,
						libmatheval_vars, monitor, now);
			break;
		case MONITOR_VALUE_T__VALUE:
			ret = rb_monitor_op_value(f, op_vars,
						libmatheval_vars, monitor, now);
			break;
		default:
			/// @todo error treatment
			rdlog(LOG_CRIT, "Unknown operation monitor type!");
			ret = NULL;
			break;
		};
	}

	delete_libmatheval_vars(libmatheval_vars);
libmatheval_error:
	evaluator_destroy(f);
	return ret;
}

struct monitor_value *process_sensor_monitor(
				struct process_sensor_monitor_ctx *process_ctx,
				const rb_monitor_t *monitor,
				rb_monitor_value_array_t *op_vars) {
	switch (monitor->type) {
#define _X(menum,cmd,type,fn)                                                  \
	case menum:                                                            \
		return fn(monitor, process_ctx, op_vars);                      \
		break;                                                         \

	MONITOR_CMDS_X
#undef _X

	default:
		rdlog(LOG_CRIT,"Unknown monitor type: %u", monitor->type);
		return NULL;
	}; /* Switch monitor type */
}

/** @note our way to save a SNMP string vector in libmatheval_array is:
    names:  [ ...  vector_name_0 ... vector_name_(N)     vector_name   ... ]
    values: [ ...     number0    ...    number(N)      split_op_result ... ]
*/

/** Process a no-vector monitor
  @param monitor Monitor to process
  @param value_buf Value in text format
  @param value Value in double format
  @param now Time of processing
*/
static struct monitor_value *process_novector_monitor(
			const rb_monitor_t *monitor, const char *value_buf,
			double value, time_t now) {
	struct monitor_value *mv = NULL;

	rd_calloc_struct(&mv, sizeof(*mv),
		-1, value_buf, &mv->value.string_value,
		RD_MEM_END_TOKEN);

	if (mv) {
		#ifdef MONITOR_VALUE_MAGIC
		mv->magic = MONITOR_VALUE_MAGIC; // just sanity check
		#endif
		mv->type = MONITOR_VALUE_T__VALUE;
		mv->name = monitor->name;
		mv->group_id = monitor->group_id;
		mv->value.timestamp = now;
		mv->value.value = value;
	} else {
		rdlog(LOG_ERR,
			"Couldn't allocate monitor value (out of memory?)");
		/// @todo memory management
	}

	return mv;
}

/** Count the number of elements of a vector response
  @param haystack String of values
  @param splittok Token that sepparate values
  @return Number of elements
  */
static size_t vector_elements(const char *haystack, const char *splittok) {
	size_t ret = 1;
	for (ret = 1; (haystack = strstr(haystack, splittok));
					haystack += strlen(splittok), ++ret);
	return ret;
}

/** Extract value of a vector
  @param vector_values String to extract values from
  @param splittok Split token
  @param str_value Value in string format
  @param str_size str_value length
  @param timestamp_sep Timestamp separator (if any)
  @param timestamp If timestamp_sep is defined, extracted timestamp
  @return Next token to iterate
  */
static bool extract_vector_value(const char *vector_values,
			const char *splittok, const char **str_value,
			size_t *str_size, double *value,
			const char *timestamp_sep, time_t *timestamp) {

	const char *end_token = strstr(vector_values, splittok);
	if (NULL == end_token) {
		end_token = vector_values + strlen(vector_values);
	}

	if (timestamp_sep) {
		assert(timestamp);

		/* Search timestamp first */
		const char *timestamp_end = strstr(vector_values,
								timestamp_sep);
		if (NULL == timestamp_end || timestamp_end >= end_token) {
			rdlog(LOG_ERR,
				"Couldn't find timestamp separator [%s] in "
				"[%*.s]", timestamp_sep,
				(int)(end_token-vector_values),vector_values);
			return false;
		}

		/// We can trust that timestamp separator token are not digits
		sscanf(vector_values, "%tu", timestamp);
		vector_values = timestamp_end + strlen(timestamp_sep);
	}

	*value = toDouble(vector_values);
	if (errno != 0) {
		char perrbuf[BUFSIZ];
		const char *errbuf = strerror_r(errno,perrbuf,sizeof(perrbuf));
		rdlog(LOG_WARNING,"Invalid double: %.*s (%s). Not counting.",
			(int)(end_token-vector_values), vector_values, errbuf);
		return false;
	}

	*str_value = vector_values;
	*str_size = end_token - vector_values;
	return true;
}

/** Process a vector monitor
  @param monitor Monitor to process
  @param value_buf Value to process (string format)
  @param number Value to process (double format)
  @param now This time
  @todo this could be joint with operation on vector
*/
static struct monitor_value *process_vector_monitor(
			const rb_monitor_t *monitor, const char *value_buf,
			double number, time_t now) {
	const size_t n_children = vector_elements(value_buf,
							monitor->splittok);
	const char *tok = NULL;

	struct monitor_value **children = calloc(n_children,
							sizeof(children[0]));
	struct monitor_value *split_op = NULL;
	if (NULL == children) {
		rdlog(LOG_ERR,
			"Couldn't allocate vector children (out of memory?)");
	}

	size_t mean_count = 0, count = 0;
	double sum = 0;
	for (count = 0, tok = value_buf; tok;
					tok = strstr(tok, monitor->splittok),
					count++) {
		if (count > 0) {
			tok += strlen(monitor->splittok);
		}

		time_t i_timestamp = 0;
		const char *i_value_str = NULL;
		size_t i_value_str_size = 0;
		double i_value = 0;

		if (0 == strcmp(tok, monitor->splittok)) {
			rdlog(LOG_DEBUG, "Not seeing value %s(%zu)",
							monitor->name, count);
			continue;
		}

		const bool get_value_rc = extract_vector_value(tok,
			monitor->splittok, &i_value_str, &i_value_str_size,
			&i_value,
			monitor->timestamp_given ? DEFAULT_TIMESTAMP_SEP : NULL,
			&i_timestamp);

		if (false == get_value_rc) {
			continue;
		}

		children[count] = process_novector_monitor(monitor,
			i_value_str, i_value, i_timestamp ? i_timestamp : now);

		if (NULL != children[count]) {
			sum += i_value;
			mean_count++;
		}
	}

	// Last token reached. Do we have an operation to do?
	if (NULL!=monitor->splitop && mean_count>0) {
		char split_op_result[1024];
		const double result = (0==strcmp("sum", monitor->splitop)) ?
			sum : sum/mean_count;

		if (isfinite(result)) {
			/// @todo check snprint result
			snprintf(split_op_result, sizeof(split_op_result),
								"%lf", result);
			split_op = process_novector_monitor(monitor,
						split_op_result, result, now);
		} else if (rd_dz(sum)) {
			rdlog(LOG_ERR,
				"%s Gives a non finite value: "
				"(sum=%lf)/(count=%zu)", monitor->name, sum,
				mean_count);
		}
	}

	return new_monitor_value_array(monitor->name, n_children, children,
								split_op);
}
