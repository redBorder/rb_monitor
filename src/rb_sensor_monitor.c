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
#include "rb_values_list.h"

#include "rb_json.h"

#include <matheval.h>
#include <librd/rdfloat.h>

#define SPLITOP_RESULT_LEN 512     /* String that will save a double */

#define VECTOR_SEP "_pos_"
#define GROUP_SEP  "_gid_"

static const char *OPERATIONS = "+-*/&^|";

#ifndef NDEBUG
#define RB_MONITOR_MAGIC 0x0b010a1c0b010a1cl
#endif

/*
 * Monitors array
 */

#define rb_monitors_array_new(count) rb_array_new(count)
#define rb_monitors_array_full(array) rb_array_full(array)
/** @note Doing with a function provides type safety */
static void rb_monitors_array_add(rb_monitors_array_t *array,
							rb_monitor_t *monitor) {
	rb_array_add(array, monitor);
}

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
	uint64_t nonzero; ///< Monitor value must not be 0
	uint64_t timestamp_given; ///< Timestamp is given in response
	uint64_t integer; ///< Response must be an integer
	const char *splittok; ///< How to split response
	const char *splitop; ///< Do a final operation with tokens
	const char *unit; ///< Monitor unit
	const char *cmd_arg; ///< Argument given to command
};

#ifdef RB_MONITOR_MAGIC
#define assert_rb_monitor(monitor) assert(RB_MONITOR_MAGIC == (monitor)->magic)
#else
#define assert_rb_monitor(monitor)
#endif

static const char *rb_monitor_type(const rb_monitor_t *monitor) {
	assert(monitor);

	switch(monitor->type) {
#define _X(menum,cmd,type,fn) case menum: return type;
		MONITOR_CMDS_X
#undef _X
	default: return NULL;
	};
}

/** Free a const string.
  @todo remove this, is ugly
  */
static void free_const_str(const char *str) {
	char *aux;
	memcpy(&aux, &str, sizeof(aux));
	free(aux);
}


/** Free resources allocated by a monitor
  @param monitor Monitor to free
  */
static void rb_monitor_done(rb_monitor_t *monitor) {
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

/** Extract an indexed monitor of a monitor array
  @param array Monitors array
  @param i Index to extract
  @return Desired monitor
  */
static rb_monitor_t *rb_monitors_array_elm_at(rb_monitors_array_t *array,
								size_t i) {
	rb_monitor_t *ret = array->elms[i];
	assert_rb_monitor(ret);
	return ret;
}

/** strtod conversion plus set errno=EINVAL if no conversion is possible
  @param str input string
  @return double
  */
static double toDouble(const char *str) {
	char * endPtr;
	errno=0;
	double d = strtod(str,&endPtr);
	if(errno==0 && endPtr==str)
		errno = EINVAL;
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

/** Parse a JSON monitor
  @param type Type of monitor (oid, system, op...)
  @param cmd_arg Argument of monitor (desired oid, system command, operation...)
  @return New monitor
  */
static rb_monitor_t *parse_rb_monitor0(enum monitor_cmd_type type,
			const char *cmd_arg, struct json_object *json_monitor) {
	const char *aux_name = PARSE_CJSON_CHILD_STR(json_monitor, "name",
									NULL);
	if (NULL == aux_name) {
		rdlog(LOG_ERR, "Monitor with no name");
		return NULL;
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
	ret->splitop = PARSE_CJSON_CHILD_STR(json_monitor, "split_op", NULL);
	ret->name = aux_name;
	ret->name_split_suffix = PARSE_CJSON_CHILD_STR(json_monitor,
						"name_split_suffix", NULL);
	ret->instance_prefix = PARSE_CJSON_CHILD_STR(json_monitor,
						"instance_prefix", NULL);
	ret->unit = PARSE_CJSON_CHILD_STR(json_monitor, "unit", NULL);
	ret->group_name = PARSE_CJSON_CHILD_STR(json_monitor,
							"group_name", NULL);
	ret->group_id = PARSE_CJSON_CHILD_STR(json_monitor, "group_id", NULL);
	ret->nonzero = PARSE_CJSON_CHILD_INT64(json_monitor, "nonzero", 0);
	ret->timestamp_given = PARSE_CJSON_CHILD_INT64(json_monitor,
							"timestamp_given", 0);
	ret->send = PARSE_CJSON_CHILD_INT64(json_monitor, "send", 1);
	ret->integer = PARSE_CJSON_CHILD_INT64(json_monitor, "integer", 0);
	ret->type = type;
	ret->cmd_arg = strdup(cmd_arg);

	return ret;
}

/** Parse a rb_monitor element
  @param json_monitor monitor in JSON format
  @todo json_monitor should be const
  @return Parsed rb_monitor.
  */
static rb_monitor_t *parse_rb_monitor(struct json_object *json_monitor,
						rb_sensor_t *monitor_sensor) {
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

rb_monitors_array_t *parse_rb_monitors(struct json_object *monitors_array_json,
					struct rb_sensor_s *sensor) {
	const size_t monitors_len = json_object_array_length(
							monitors_array_json);
	rb_monitors_array_t *ret = rb_monitors_array_new(monitors_len);

	for (size_t i=0; i<monitors_len; ++i) {
		if (rb_monitors_array_full(ret)) {
			rdlog(LOG_CRIT,
				"Sensors array full at %zu, can't add %zu",
				ret->size, i);
			break;
		}

		json_object *monitor_json = json_object_array_get_idx(
							monitors_array_json, i);
		rb_monitor_t *monitor = parse_rb_monitor(monitor_json, sensor);
		if (monitor) {
			rb_monitors_array_add(ret, monitor);
		}
	}

	return ret;
}

/** Context of sensor monitors processing */
struct process_sensor_monitor_ctx {
	rd_memctx_t memctx; ///< Memory context
	struct _worker_info *worker_info; ///< All workers shared data
	struct monitor_snmp_session *snmp_sessp; ///< Base SNMP session
	struct libmatheval_stuffs *libmatheval_variables; ///< libmatheval vars
	/// Bad marked variables
	struct {
		/// @TODO change by an array/trie/hashtable/something, or
		/// rb_array
		const char **names;
		size_t pos;
		size_t size;
	} bad_names;
};

/* FW declaration */
static void process_sensor_monitor(
				struct process_sensor_monitor_ctx *process_ctx,
				const rb_monitor_t *monitor,
				const rb_sensor_t *sensor, rd_lru_t *ret);
static int process_novector_monitor(struct _worker_info *worker_info,
		const rb_monitor_t *monitor, const rb_sensor_t *sensor,
		struct libmatheval_stuffs* libmatheval_variables,
		const char *value_buf,
		double value, rd_lru_t *valueslist);

static int process_vector_monitor(struct _worker_info *worker_info,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			struct libmatheval_stuffs* libmatheval_variables,
			char *value_buf, rd_lru_t *valueslist,
			rd_memctx_t *memctx);

bool process_monitors_array(struct _worker_info *worker_info,
			rb_sensor_t *sensor, rb_monitors_array_t *monitors,
			struct snmp_params_s *snmp_params,
			rd_lru_t *ret) {
	bool aok = true;
	struct process_sensor_monitor_ctx process_ctx = {
		.worker_info = worker_info,
		.libmatheval_variables = new_libmatheval_stuffs(
							monitors->count*10),
		.bad_names = {
			.names = calloc(monitors->count,
				sizeof(process_ctx.bad_names.names[0])),
			.size = monitors->count,
		},
	};
	rd_memctx_init(&process_ctx.memctx,NULL,RD_MEMCTX_F_TRACK);

	/* @todo we only need this if we are going to use SNMP */
	if (NULL == snmp_params->peername) {
		aok = false;
		rdlog(LOG_ERR, "Peername not setted in %s. Skipping.",
							rb_sensor_name(sensor));
	}

	if (NULL == snmp_params->session.community) {
		aok = false;
		rdlog(LOG_ERR, "Community not setted in %s. Skipping.",
							rb_sensor_name(sensor));
	}

	if(aok) {
		pthread_mutex_lock(&worker_info->snmp_session_mutex);
		/* @TODO: You can do it later, see session_api.h */
		worker_info->default_session.peername =
						(char *)snmp_params->peername;
		const struct monitor_snmp_new_session_config config = {
			snmp_params->session.community,
			snmp_params->session.timeout,
			worker_info->default_session.flags,
			snmp_params->session.version
		};
		process_ctx.snmp_sessp = new_snmp_session(&worker_info->default_session,&config);
		if(NULL== process_ctx.snmp_sessp){
			rdlog(LOG_ERR,"Error creating session: %s",snmp_errstring(worker_info->default_session.s_snmp_errno));
			aok=0;
		}
		pthread_mutex_unlock(&worker_info->snmp_session_mutex);
	}

	for (size_t i=0; aok && i<monitors->count; ++i) {
		process_sensor_monitor(&process_ctx,
			rb_monitors_array_elm_at(monitors, i), sensor, ret);
	}
	destroy_snmp_session(process_ctx.snmp_sessp);

	if(aok) {
		rd_memctx_freeall(&process_ctx.memctx);
		delete_libmatheval_stuffs(process_ctx.libmatheval_variables);
	}
	return aok;
}

/** Base function to obtain an external value, and to manage it as a vector or
  as an integer
  @param worker_info Common thread information
  @param process_ctx Processing context
  @param monitor Monitor to process
  @param send Send value to kafka
  @param valueslist List of values processed by this sensor in this iteration
  @param get_value_cb Callback to get value
  @param get_value_cb_ctx Context send to get_value_cb
  @return true if we can send value
  */
static bool rb_monitor_get_external_value(struct _worker_info *worker_info,
		struct process_sensor_monitor_ctx *process_ctx,
		const rb_monitor_t *monitor, const rb_sensor_t *sensor,
		bool *send, rd_lru_t *valueslist,
		bool (*get_value_cb)(char *buf, size_t bufsiz, double *number,
						void *ctx, const char *arg),
		void *get_value_cb_ctx) {
	double number = 0;
	char value_buf[BUFSIZ];
	value_buf[0] = '\0';
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
		process_novector_monitor(worker_info, monitor, sensor,
				process_ctx->libmatheval_variables, value_buf,
				number, valueslist);
	} else /* We have a vector here */ {
		/** @todo delete parameters that could be sent
			using monitor */
		process_vector_monitor(worker_info, monitor, sensor,
			process_ctx->libmatheval_variables, value_buf,
			valueslist, &process_ctx->memctx);
	}

	/** @todo move to process_novector_monitor */
	if(monitor->nonzero && rd_dz(number))
	{
		assert(process_ctx->bad_names.pos
					< process_ctx->bad_names.size);
		/// @TODO be able to difference between system and oid
		/// in error message
		rdlog(LOG_ALERT,
			"value oid=%s is 0, but nonzero setted. skipping.",
			monitor->cmd_arg);
		size_t bad_names_pos = process_ctx->bad_names.pos++;
		process_ctx->bad_names.names[bad_names_pos++] = monitor->name;
		*send = 0;
	}

	return ok;
}

/** Convenience function to obtain system values */
static bool rb_monitor_get_system_external_value(
			struct _worker_info *worker_info, bool *send,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			rd_lru_t *valueslist,
			struct process_sensor_monitor_ctx *process_ctx) {
	return rb_monitor_get_external_value(worker_info, process_ctx, monitor,
		sensor, send, valueslist, system_solve_response, NULL);
}

/** Convenience function */
static bool snmp_solve_response0(char *value_buf, size_t value_buf_len,
			double *number, void *session, const char *oid_string) {
	return snmp_solve_response(value_buf, value_buf_len, number,
		(struct monitor_snmp_session *)session, oid_string);
}

/** Convenience function to obtain SNMP values */
static bool rb_monitor_get_snmp_external_value(
			struct _worker_info *worker_info, bool *send,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			rd_lru_t *valueslist,
			struct process_sensor_monitor_ctx *process_ctx) {
	return rb_monitor_get_external_value(worker_info, process_ctx, monitor,
		sensor, send, valueslist, snmp_solve_response0,
				process_ctx->snmp_sessp);
}

/** Convenience function to obtain operations values */
static bool rb_monitor_get_op_result(
			struct _worker_info *worker_info, bool *send,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			rd_lru_t *valueslist,
			struct process_sensor_monitor_ctx *process_ctx) {
	double number = 0;
	const char * operation = monitor->cmd_arg;
	bool op_ok = true;
	struct libmatheval_stuffs *libmatheval_variables =
					process_ctx->libmatheval_variables;

	for (unsigned int i=0; op_ok==1 && i<process_ctx->bad_names.pos; ++i) {
		if (strstr(process_ctx->bad_names.names[i],operation)) {
			rdlog(LOG_NOTICE,
				"OP %s Uses a previously bad marked value variable (%s). Skipping",
				operation,
				process_ctx->bad_names.names[i]);
			*send = op_ok = false;
		}
	}

	if(op_ok)
	{
		struct vector_variables_s { // @TODO Integrate in struct monitor_value
			char *name;
			size_t name_len;
			unsigned int pos;
			SIMPLEQ_ENTRY(vector_variables_s) entry;
		};
		SIMPLEQ_HEAD(listhead,vector_variables_s) head = SIMPLEQ_HEAD_INITIALIZER(head);

		/* TODO: use libmatheval_names better */
		char *str_op_variables = strdup(operation);
		char *str_op = NULL;
		char *auxchar;
		char *tok = strtok_r(str_op_variables,OPERATIONS,&auxchar);

		SIMPLEQ_INIT(&head);
		size_t vectors_len=1;
		size_t vector_variables_count = 0;
		while(tok) /* searching if some variable is a vector */ {
			size_t pos, size;
			if(libmatheval_search_vector(libmatheval_variables->names,
				libmatheval_variables->variables_pos,tok,&pos,&size) && size>1)
			{
				struct vector_variables_s *vvs = malloc(sizeof(struct vector_variables_s));
				vvs->name     = tok;
				vvs->name_len = strlen(tok);
				vvs->pos      = pos;
				SIMPLEQ_INSERT_TAIL(&head,vvs,entry);
				vector_variables_count++;

				if(vectors_len==1)
				{
					vectors_len=size;
				}
				else if(vectors_len!=size)
				{
					rdlog(LOG_ERR,"vector dimension mismatch");
					op_ok=0;
				}
			}
			tok = strtok_r(NULL,OPERATIONS,&auxchar);
		}

		if(vector_variables_count>0)
		{
			/* +6: to add _pos_99999 to all variables */
			str_op = calloc(strlen(operation)+10*vector_variables_count,sizeof(char)); // @TODO more descriptive name
		}
		else
		{
			str_op = (char *)operation;
		}

		double sum=0;unsigned count=0;

		struct monitor_value monitor_value; // ready-to-go struct
		memset(&monitor_value,0,sizeof(monitor_value));
		#ifdef MONITOR_VALUE_MAGIC
		monitor_value.magic = MONITOR_VALUE_MAGIC; // just sanity check
		#endif
		monitor_value.timestamp = time(NULL);
		monitor_value.sensor_id = rb_sensor_id(sensor);
		monitor_value.sensor_name = rb_sensor_name(sensor);
		monitor_value.instance_prefix = monitor->instance_prefix;
		monitor_value.bad_value = 0;
		monitor_value.unit = monitor->unit;
		monitor_value.group_name = monitor->group_name;
		monitor_value.group_id = monitor->group_id;
		monitor_value.type = rb_monitor_type(monitor);
		monitor_value.enrichment = rb_sensor_enrichment(sensor);

		for(size_t j=0;op_ok && j<vectors_len;++j) /* foreach member of vector */
		{
			// @TODO make the suffix append in libmatheval_append
			const size_t namelen = strlen(monitor->name);
			char *mathname = rd_memctx_calloc(
				&process_ctx->memctx,
				namelen+strlen(VECTOR_SEP)+6,
				/* space enough to save _60000 */
				sizeof(char));
			strcpy(mathname,monitor->name);
			mathname[namelen] = '\0';

			/* editing operation string, so evaluate() will put the correct result */

			const char * operation_iterator = operation;
			char * str_op_iterator    = str_op;
			struct vector_variables_s *vector_iterator;
			/* It should be a short operation. If needed, we will cache it and just change the _ suffix */
			SIMPLEQ_FOREACH(vector_iterator,&head,entry)
			{
				// sustitute each variable => variable_idx:
				//   copyng all unchanged string before the variable
				const char * operator_pos = strstr(operation_iterator,vector_iterator->name);
				assert(operator_pos);
				const size_t unchanged_size = operator_pos - operation_iterator;
				strncpy(str_op_iterator,operation_iterator,unchanged_size);

				str_op_iterator += unchanged_size;

				strcpy(str_op_iterator,libmatheval_variables->names[vector_iterator->pos+j]);

				str_op_iterator    += unchanged_size + strlen(libmatheval_variables->names[vector_iterator->pos+j]);
				operation_iterator += unchanged_size + vector_iterator->name_len;

			}
			rdlog(LOG_DEBUG,"vector operation string result: %s",str_op);

			/* extracting suffix */
			if(vectors_len>1)
			{
				const int mathpos = SIMPLEQ_FIRST(&head)->pos + j;
				const char * suffix = strrchr(libmatheval_variables->names[mathpos],'_');
				if(suffix)
				{
					strcpy(mathname+namelen,VECTOR_SEP);
					strcpy(mathname+namelen+strlen(VECTOR_SEP),suffix+1);
				}
			}



			void * const f = evaluator_create ((char *)str_op); /* really it has to be (void *). See libmatheval doc. */
		                                                       /* also, we have to create a new one for each iteration */
			if(NULL==f)
			{
				rdlog(LOG_ERR,"Operation not valid: %s",str_op);
				op_ok = 0;
			}

			if(op_ok){
				char ** evaluator_variables;int evaluator_variables_count,vars_pos;
				evaluator_get_variables(f,&evaluator_variables,&evaluator_variables_count);
				op_ok=libmatheval_check_exists(evaluator_variables,evaluator_variables_count,libmatheval_variables,&vars_pos);
				if(!op_ok)
				{
					rdlog(LOG_ERR,"Variable not found: %s",evaluator_variables[vars_pos]);
					evaluator_destroy(f);
				}
			}

			if(op_ok && NULL!=f)
			{
				number = evaluator_evaluate (f, libmatheval_variables->variables_pos,
					(char **) libmatheval_variables->names,	libmatheval_variables->values);
				evaluator_destroy (f);
				rdlog(LOG_DEBUG,
					"Result of operation %s: %lf",
					monitor->cmd_arg,number);
				if(rd_dz(number) == 0
					&& monitor->nonzero)
				{
					op_ok=false;
					rdlog(LOG_ERR,"OP %s return 0, and nonzero setted. Skipping.",operation);
				}
				if(!isnormal(number))
				{
					op_ok=false;
					rdlog(LOG_ERR,"OP %s return a bad value: %lf. Skipping.",operation,number);
				}
				/* op will send by default, so we ignore kafka param */
			}

			if(op_ok && libmatheval_append(libmatheval_variables, mathname,number))
			{
				char val_buf[64];
				sprintf(val_buf,"%lf",number);


				if(vectors_len>1)
				{
					char name_buf[1024];
					char *vector_pos = NULL;
					sprintf(name_buf, "%s%s",
						monitor->name,
						monitor->name_split_suffix);
					monitor_value.name = name_buf;
					if((vector_pos = strstr(mathname,VECTOR_SEP)))
					{
						monitor_value.instance_valid = 1;
						monitor_value.instance = atoi(vector_pos + strlen(VECTOR_SEP));
					}
					else
					{
						monitor_value.instance_valid = 0;
						monitor_value.instance = 0;
					}
					monitor_value.value=number;
					monitor_value.string_value=val_buf;
				}
				else
				{
					monitor_value.name
							= monitor->name;
					monitor_value.value = number;
					monitor_value.instance_valid = 0;
					monitor_value.string_value=val_buf;
				}
				const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

				if ((send) && new_mv)
					rd_lru_push(valueslist, (void *)new_mv);
			}
			if(op_ok && monitor->splitop) {
				sum+=number;
				count++;
			}
			mathname=NULL;

		} /* foreach member of vector */
		if(monitor->splitop)
		{
			if(0==strcmp(monitor->splitop, "sum"))
				number = sum;
			else if(0==strcmp(monitor->splitop, "mean"))
				number = sum/count;
			else
				op_ok=0;

			if(op_ok){
				char split_op_result[64];
				sprintf(split_op_result,"%lf",number);
				monitor_value.name = monitor->name;
				monitor_value.value = number;
				monitor_value.instance_valid = 0;
				monitor_value.string_value=split_op_result;

				const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

				if ((send) && new_mv) {
					rd_lru_push(valueslist, (void *)new_mv);
				}
			}
		}

		struct vector_variables_s *vector_iterator,*tmp_vector_iterator;
		SIMPLEQ_FOREACH_SAFE(vector_iterator,&head,entry,tmp_vector_iterator)
		{
			free(vector_iterator);
		}
		if(vector_variables_count>0) // @TODO: make it more simple
			free(str_op);
		free(str_op_variables);
	}

	return op_ok;
}

/** Process a sensor monitor
  @param process_ctx Process context
  @param monitor Monitor to process
  @param ret Returned messages
  */
static void process_sensor_monitor(
				struct process_sensor_monitor_ctx *process_ctx,
				const rb_monitor_t *monitor,
				const rb_sensor_t *sensor, rd_lru_t *ret) {
	struct _worker_info *worker_info = process_ctx->worker_info;
	rd_lru_t *valueslist = rd_lru_new();
	bool send = monitor->send;

	switch (monitor->type) {
#define _X(menum,cmd,type,fn)                                                  \
	case menum:                                                            \
		fn(worker_info, &send, monitor, sensor, valueslist,            \
						process_ctx);                  \
		break;                                                         \

	MONITOR_CMDS_X
#undef _X

	default:
		rdlog(LOG_CRIT,"Unknown monitor type: %u", monitor->type);
	}; /* Switch monitor type */

	const struct monitor_value * monitor_value = NULL;
	while((monitor_value = rd_lru_pop(valueslist))) {
		struct printbuf* printbuf= print_monitor_value(monitor_value);
		if(likely(NULL!=printbuf)) {
			if(send) {
				char *dup = strdup(printbuf->buf);
				if (NULL == dup) {
					rdlog(LOG_ERR, "Couldn't dup!");
				} else {
					rd_lru_push(ret, dup);
				}
			}
		} /* for i in splittoks */
		printbuf_free(printbuf);
	}

	rd_lru_destroy(valueslist);
}

/** @note our way to save a SNMP string vector in libmatheval_array is:
    names:  [ ...  vector_name_0 ... vector_name_(N)     vector_name   ... ]
    values: [ ...     number0    ...    number(N)      split_op_result ... ]
*/

/** Process a no-vector monitor */
// @todo pass just a monitor_value with all precached possible.
static int process_novector_monitor(struct _worker_info *worker_info,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			struct libmatheval_stuffs* libmatheval_variables,
			const char *value_buf,
			double value, rd_lru_t *valueslist) {

	int aok = 1;
	if(likely(libmatheval_append(libmatheval_variables,
						monitor->name, value))) {
		struct monitor_value monitor_value;
		memset(&monitor_value,0,sizeof(monitor_value));
		#ifdef MONITOR_VALUE_MAGIC
		monitor_value.magic = MONITOR_VALUE_MAGIC; // just sanity check
		#endif
		monitor_value.timestamp = time(NULL);
		monitor_value.sensor_name = rb_sensor_name(sensor);
		monitor_value.sensor_id = rb_sensor_id(sensor);
		monitor_value.name = monitor->name;
		monitor_value.instance = 0;
		monitor_value.instance_valid = 0;
		monitor_value.bad_value = 0;
		monitor_value.value = value;
		monitor_value.string_value = value_buf;
		monitor_value.unit = monitor->unit;
		monitor_value.group_name = monitor->group_name;
		monitor_value.group_id = monitor->group_id;
		monitor_value.integer = monitor->integer;
		monitor_value.type = rb_monitor_type(monitor);
		monitor_value.enrichment = rb_sensor_enrichment(sensor);

		/** @TODO monitor_values_tree should be by sensor, not general!
		*/
		const struct monitor_value * new_mv = update_monitor_value(
			worker_info->monitor_values_tree,&monitor_value);

		if(monitor->send && new_mv) {
			rd_lru_push(valueslist,(void *)new_mv);
		}
	}
	else
	{
		rdlog(LOG_ERR,"Error adding libmatheval value");
		aok = 0;
	}

	return aok;
}


// @todo pass just a monitor_value with all precached possible.
// @todo make a call to process_novector_monitor
static int process_vector_monitor(struct _worker_info *worker_info,
			const rb_monitor_t *monitor, const rb_sensor_t *sensor,
			struct libmatheval_stuffs* libmatheval_variables,
			char *value_buf, rd_lru_t *valueslist,
			rd_memctx_t *memctx) {
	time_t last_valid_timestamp = 0;

	int aok=1;
	char *tok = value_buf;
	const char *name = monitor->name;

	assert(worker_info);
	assert(monitor);
	assert(libmatheval_variables);
	assert(valueslist);

	assert(memctx);


	struct monitor_value monitor_value;
	memset(&monitor_value,0,sizeof(monitor_value));
	#ifdef MONITOR_VALUE_MAGIC
	monitor_value.magic = MONITOR_VALUE_MAGIC; // just sanity check
	#endif
	monitor_value.sensor_name = rb_sensor_name(sensor);
	monitor_value.sensor_id = rb_sensor_id(sensor);
	monitor_value.bad_value = 0;
	monitor_value.unit = monitor->unit;
	monitor_value.group_name = monitor->group_name;
	monitor_value.group_id = monitor->group_id;
	monitor_value.integer = monitor->integer;
	monitor_value.type = rb_monitor_type(monitor);
	monitor_value.enrichment = rb_sensor_enrichment(sensor);

	const size_t per_instance_name_len = strlen(name) +
		(monitor->name_split_suffix ?
			strlen(monitor->name_split_suffix)
			:0)
		+ 1;
	char per_instance_name[per_instance_name_len];
	snprintf(per_instance_name, per_instance_name_len, "%s%s", name,
		monitor->name_split_suffix);

	unsigned int count = 0,mean_count=0;
	double sum=0;
	const size_t name_len = strlen(name);
	while(tok){
		time_t timestamp = 0;
		char *nexttok = strstr(tok,monitor->splittok);
		if(nexttok != NULL && *nexttok != '\0')
		{
			*nexttok = '\0';
			nexttok++;
		}
		if(*tok)
		{
			char * tok_name = rd_memctx_calloc(memctx,name_len+strlen(GROUP_SEP)+7+strlen(VECTOR_SEP)+7,sizeof(char)); /* +7: space to allocate _65535 */

			if(monitor->timestamp_given)
			{
				char * aux;
				const char * ts_tok = strtok_r(tok,":",&aux);
				tok = strtok_r(NULL,":",&aux);
				timestamp = toDouble(ts_tok);
				if(0!=errno)
				{
					char buf[1024];
					strerror_r(errno,buf,sizeof(buf));
					rdlog(LOG_WARNING,"Invalid double %s:%s. Assigned current timestamp",tok,buf);
					tok = nexttok;
					timestamp = time(NULL);
				}
			}
			else
			{
				timestamp = time(NULL);
			}

			if(tok && *tok)
			{

				errno=0;
				double tok_f = toDouble(tok);
				if(errno==0)
				{
					if(monitor->group_id)
						snprintf(tok_name, name_len+7+7,
							"%s" GROUP_SEP "%s"
								VECTOR_SEP "%u",
							name, monitor->group_id,
							count);
					else
						snprintf(tok_name, name_len+7+7,
							"%s" VECTOR_SEP "%u",
							name, count);

					if(likely(0!=libmatheval_append(libmatheval_variables,tok_name,atof(tok))))
					{
						monitor_value.timestamp =
								timestamp;
						monitor_value.name = tok_name;
						monitor_value.send_name =
							per_instance_name;
						monitor_value.instance = count;
						monitor_value.instance_prefix =
							monitor->instance_prefix;
						monitor_value.instance_valid =
									1;
						monitor_value.value=tok_f;
						monitor_value.string_value=tok;

						const struct monitor_value *new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

						if(new_mv)
						{
							last_valid_timestamp = timestamp;
							if(monitor->send)
								rd_lru_push(valueslist,(void *)new_mv);
						}
					}
					else
					{
						rdlog(LOG_ERR,"Error adding libmatheval value");
						aok = 0;
					}

					if (NULL!=monitor->splitop)
					{
						sum += atof(tok);
						mean_count++;
					}
				}
				else
				{
					rdlog(LOG_WARNING,"Invalid double: %s. Not counting.",tok);
				} /* valid double */
			}
		}
		else /* *tok==0 */
		{
			rdlog(LOG_DEBUG,"Not seeing value %s(%d)",name,count);
		}

		count++;
		tok = nexttok;
	} /* while(tok) */

	// Last token reached. Do we have an operation to do?
	if (NULL!=monitor->splitop && mean_count>0)
	{
		char split_op_result[1024];
		double result = 0;
		if (0==strcmp("sum",monitor->splitop))
		{
			result = sum;
		}
		else if (0==strcmp("mean",monitor->splitop)){
			result = sum/mean_count;
		}
		else
		{
			rdlog(LOG_WARNING,
				"Splitop %s unknow in monitor parameter %s",
				monitor->splitop, name);
		}


		const int splitop_is_valid = isfinite(result);

		if(splitop_is_valid)
		{
			snprintf(split_op_result,SPLITOP_RESULT_LEN,"%lf",result);
			if(0==libmatheval_append(libmatheval_variables,name,result))
			{
				rdlog(LOG_WARNING,"Cannot save %s -> %s in libmatheval_variables.",
					name,split_op_result);
			}
			else
			{
				monitor_value.timestamp = last_valid_timestamp;
				monitor_value.name = name;
				monitor_value.send_name = NULL;
				monitor_value.instance = 0;
				monitor_value.instance_prefix = NULL;
				monitor_value.instance_valid = 0;
				monitor_value.value = result;
				monitor_value.string_value = split_op_result;
				monitor_value.group_name = monitor->group_name;
				monitor_value.group_id = monitor->group_id;
//				monitor_value.type = type;

				const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

				if(monitor->send && new_mv)
					rd_lru_push(valueslist,(void *)new_mv);
			}
		}
		else
		{
			if(rd_dz(sum))
				rdlog(LOG_ERR,"%s Gives a non finite value: (sum=%lf)/(count=%u)",name,sum,mean_count);
		}

	}

	return aok;
}

void rb_monitors_array_done(rb_monitors_array_t *monitors_array) {
	for (size_t i=0; i<monitors_array->count; ++i) {
		rb_monitor_done(rb_monitors_array_elm_at(monitors_array, i));
	}
}
