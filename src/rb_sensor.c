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

#include "rb_snmp.h"
#include "rb_system.h"
#include "rb_libmatheval.h"
#include "rb_values_list.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>
#include <matheval.h>

#define SPLITOP_RESULT_LEN 512     /* String that will save a double */

#define VECTOR_SEP "_pos_"
#define GROUP_SEP  "_gid_"

static const char *OPERATIONS = "+-*/&^|";

/// Sensor data
typedef struct {
	uint64_t timeout;        ///< Requests timeout
	char *peername;          ///< Requests sensor peername (ip/addr)
	json_object *enrichment; ///< Enrichment to use in monitors
	char *sensor_name;       ///< Sensor name to append in monitors
	uint64_t sensor_id;      ///< Sensor id to append in monitors
	char *community;         ///< SNMP community
	long snmp_version;       ///< SNMP version
} sensor_data_t;

/// Sensor to monitor
struct rb_sensor_s {
#ifndef NDEBUG
#define RB_SENSOR_MAGIC 0xB30A1CB30A1CL
	uint64_t magic;
#endif

	sensor_data_t data;              ///< Data of sensor
	/// @TODO make a monitors array!
	json_object *monitors;           ///< Monitors to ask for
	int refcnt;                      ///< Reference counting
};

#ifdef RB_SENSOR_MAGIC
void assert_rb_sensor(rb_sensor_t *sensor) {
	assert(RB_SENSOR_MAGIC == sensor->magic);
}
#endif

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

/** @note our way to save a SNMP string vector in libmatheval_array is:
    names:  [ ...  vector_name_0 ... vector_name_(N)     vector_name   ... ]
    values: [ ...     number0    ...    number(N)      split_op_result ... ]
*/

// @todo pass just a monitor_value with all precached possible.
static int process_novector_monitor(struct _worker_info *worker_info,sensor_data_t * sensor_data, struct libmatheval_stuffs* libmatheval_variables,
	const char *name,const char * value_buf,double value, rd_lru_t *valueslist,	const char * unit, const char * group_name,const char * group_id,
	const char *(*type)(void),const json_object *enrichment,int send, int integer)
{
	int aok = 1;
	if(likely(libmatheval_append(libmatheval_variables,name,value)))
	{
		struct monitor_value monitor_value;
		memset(&monitor_value,0,sizeof(monitor_value));
		#ifdef MONITOR_VALUE_MAGIC
		monitor_value.magic = MONITOR_VALUE_MAGIC; // just sanity check
		#endif
		monitor_value.timestamp = time(NULL);
		monitor_value.sensor_name = sensor_data->sensor_name;
		monitor_value.sensor_id = sensor_data->sensor_id;
		monitor_value.name = name;
		monitor_value.instance = 0;
		monitor_value.instance_valid = 0;
		monitor_value.bad_value = 0;
		monitor_value.value=value;
		monitor_value.string_value=value_buf;
		monitor_value.unit=unit;
		monitor_value.group_name=group_name;
		monitor_value.group_id=group_id;
		monitor_value.integer=integer;
		monitor_value.type = type;
		monitor_value.enrichment = enrichment;

		const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

		if(send && new_mv)
			rd_lru_push(valueslist,(void *)new_mv);
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
static int process_vector_monitor(struct _worker_info *worker_info,sensor_data_t * sensor_data, struct libmatheval_stuffs* libmatheval_variables,
	const char *name,char * value_buf, const char *splittok, rd_lru_t *valueslist,const char *unit,const char *group_id,const char *group_name,
	const char * instance_prefix,const char * name_split_suffix,const char * splitop,const json_object *enrichment,int send,int timestamp_given,const char *(*type)(void),rd_memctx_t *memctx,int integer)
{
	assert(worker_info);
	assert(sensor_data);
	assert(libmatheval_variables);

	assert(name);
	assert(value_buf);
	assert(splittok);
	// assert(valueslist);
	// assert(instance_prefix);
	// assert(name_split_suffix);
	// assert(splitop); <- May be NULL
	assert(memctx);

	time_t last_valid_timestamp=0;

	int aok=1;
	char * tok = value_buf; // Note: can't use strtok_r because value_buf with no values,
					        // i.e., just many splittok.

	struct monitor_value monitor_value;
	memset(&monitor_value,0,sizeof(monitor_value));
	#ifdef MONITOR_VALUE_MAGIC
	monitor_value.magic = MONITOR_VALUE_MAGIC; // just sanity check
	#endif
	monitor_value.sensor_name = sensor_data->sensor_name;
	monitor_value.sensor_id = sensor_data->sensor_id;
	monitor_value.bad_value = 0;
	monitor_value.unit=unit;
	monitor_value.group_name=group_name;
	monitor_value.group_id=group_id;
	monitor_value.integer=integer;
	monitor_value.type = type;
	monitor_value.enrichment = enrichment;

	const size_t per_instance_name_len = strlen(name) + (name_split_suffix?strlen(name_split_suffix):0) + 1;
	char per_instance_name[per_instance_name_len];
	snprintf(per_instance_name,per_instance_name_len,"%s%s",name,name_split_suffix);


	unsigned int count = 0,mean_count=0;
	double sum=0;
	const size_t name_len = strlen(name);
	while(tok){
		time_t timestamp = 0;
		char * nexttok = strstr(tok,splittok);
		if(nexttok != NULL && *nexttok != '\0')
		{
			*nexttok = '\0';
			nexttok++;
		}
		if(*tok)
		{
			char * tok_name = rd_memctx_calloc(memctx,name_len+strlen(GROUP_SEP)+7+strlen(VECTOR_SEP)+7,sizeof(char)); /* +7: space to allocate _65535 */

			if(timestamp_given)
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
					if(group_id)
						snprintf(tok_name,name_len+7+7,"%s" GROUP_SEP "%s" VECTOR_SEP "%u",name,group_id,count);
					else
						snprintf(tok_name,name_len+7+7,"%s" VECTOR_SEP "%u",name,count);

					if(likely(0!=libmatheval_append(libmatheval_variables,tok_name,atof(tok))))
					{
						monitor_value.timestamp = timestamp;
						monitor_value.name = tok_name;
						monitor_value.send_name = per_instance_name;
						monitor_value.instance = count;
						monitor_value.instance_prefix = instance_prefix;
						monitor_value.instance_valid = 1;
						monitor_value.value=tok_f;
						monitor_value.string_value=tok;

						const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

						if(new_mv)
						{
							last_valid_timestamp = timestamp;
							if(send)
								rd_lru_push(valueslist,(void *)new_mv);
						}
					}
					else
					{
						rdlog(LOG_ERR,"Error adding libmatheval value");
						aok = 0;
					}

					if(NULL!=splitop)
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
	if(NULL!=splitop && mean_count>0)
	{
		char split_op_result[1024];
		double result = 0;
		if(0==strcmp("sum",splitop))
		{
			result = sum;
		}
		else if(0==strcmp("mean",splitop)){
			result = sum/mean_count;
		}
		else
		{
			rdlog(LOG_WARNING,"Splitop %s unknow in monitor parameter %s",splitop,name);
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
				monitor_value.value=result;
				monitor_value.string_value=split_op_result;
				monitor_value.group_name=group_name;
				monitor_value.group_id=group_id;
//				monitor_value.type = type;

				const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

				if(send && new_mv)
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



/** Process all monitors in sensor, returning result in ret
  @param worker_info All workers info
  @param sensor_data Data of current sensor
  @param monitors Array of monitors to ask
  @param ret Message returning function
  @warning This function assumes ALL fields of sensor_data will be populated */
static bool process_sensor_monitors(struct _worker_info *worker_info,
		sensor_data_t *sensor_data, json_object *monitors,
		rd_lru_t *ret) {
	bool aok = true;
	struct libmatheval_stuffs *libmatheval_variables = new_libmatheval_stuffs(json_object_array_length(monitors)*10);
	struct monitor_snmp_session * snmp_sessp=NULL;
	rd_memctx_t memctx;
	memset(&memctx,0,sizeof(memctx));
	rd_memctx_init(&memctx,NULL,RD_MEMCTX_F_TRACK);

	const char *bad_names[json_object_array_length(monitors)]; /* ops cannot be added to libmatheval array.*/
	size_t bad_names_pos = 0;

	assert(sensor_data->sensor_name);
	check_setted(sensor_data->peername,&aok,
		"Peername not setted in %s. Skipping.",sensor_data->sensor_name);
	check_setted(sensor_data->community,&aok,
		"Community not setted in %s. Skipping.",sensor_data->sensor_name);


	if(aok){
		pthread_mutex_lock(&worker_info->snmp_session_mutex);
		/* @TODO: You can do it later, see session_api.h */
		worker_info->default_session.peername = (char *)sensor_data->peername;
		const struct monitor_snmp_new_session_config config = {
			sensor_data->community,
			sensor_data->timeout,
			worker_info->default_session.flags,
			sensor_data->snmp_version
		};
		snmp_sessp = new_snmp_session(&worker_info->default_session,&config);
		if(NULL== snmp_sessp){
			rdlog(LOG_ERR,"Error creating session: %s",snmp_errstring(worker_info->default_session.s_snmp_errno));
			aok=0;
		}
		pthread_mutex_unlock(&worker_info->snmp_session_mutex);
	}

	for(int i=0; aok && i<json_object_array_length(monitors);++i){
		const char * name=NULL,*name_split_suffix = NULL,*instance_prefix=NULL,*group_id=NULL,*group_name=NULL;
		json_object *monitor_parameters_array = json_object_array_get_idx(monitors, i);
		uint64_t send = 1, nonzero=0,timestamp_given=0,integer=0;
		const char * splittok=NULL,*splitop=NULL;
		const char * unit=NULL;
		double number;int valid_double;

		rd_lru_t * valueslist    = rd_lru_new();


		/* First pass: get attributes except op: and oid:*/
		json_object_object_foreach(monitor_parameters_array,key2,val2)
		{
			errno=0;
			if(0==strncmp(key2,"split",strlen("split")+1)){
				splittok = json_object_get_string(val2);
			}else if(0==strncmp(key2,"split_op",strlen("split_op"))){
				splitop = json_object_get_string(val2);
			}else if(0==strncmp(key2,"name",strlen("name")+1)){
				name = json_object_get_string(val2);
			}else if(0==strncmp(key2,"name_split_suffix",strlen("name_split_suffix"))){
				name_split_suffix = json_object_get_string(val2);
			}else if(0==strcmp(key2,"instance_prefix")){
				instance_prefix = json_object_get_string(val2);
			}else if(0==strncmp(key2,"unit",strlen("unit"))){
				unit = json_object_get_string(val2);
			}else if(0==strncmp(key2,"group_name",strlen("group_name"))){
				group_name = json_object_get_string(val2);
			}else if(0==strncmp(key2,"group_id",strlen("group_id"))){
				group_id = json_object_get_string(val2);
			}else if(0==strcmp(key2,"nonzero")){
				nonzero = 1;
			}else if(0==strcmp(key2,"timestamp_given")){
				timestamp_given=json_object_get_int64(val2);
			} else if (0 == strncmp(key2, "send", strlen("send"))) {
				send = json_object_get_int64(val2);
			}else if(0==strcmp(key2,"integer")){
				integer = json_object_get_int64(val2);
			}else if(0==strncmp(key2,"oid",strlen("oid")) || 0==strncmp(key2,"op",strlen("op"))){
				// will be resolved in the next foreach
			}else if(0==strncmp(key2,"system",strlen("system"))){
				// will be resolved in the next foreach
			}else{
				rdlog(LOG_ERR,"Cannot parse %s argument",key2);
			}

			if(errno!=0){
				rdlog(LOG_ERR,"Could not parse %s value: %s",key2,strerror(errno));
			}

		} /* foreach */

		if(unlikely(NULL==sensor_data->sensor_name)){
			rdlog(LOG_ERR,"sensor name not setted. Skipping.");
			break;
		}

		json_object_object_foreach(monitor_parameters_array,key,val){
			errno=0;
			if(0==strncmp(key,"oid",strlen("oid")) || 0==strncmp(key,"system",strlen("system"))){
				// @TODO refactor all this block. Search for repeated code.
				/* @TODO test passing a sensor without params to caller function. */
				if(unlikely(!name)){
					rdlog(LOG_WARNING,"name of param not set in %s. Skipping",
						sensor_data->sensor_name);
					break /*foreach*/;
				}

				char value_buf[1024] = {'\0'};
				const char *(*type_fn)(void) = NULL;
				if(0==strcmp(key,"oid"))
				{
					type_fn = snmp_type_fn;
					valid_double = snmp_solve_response(value_buf,1024,&number,snmp_sessp,json_object_get_string(val));
				}
				else
				{
					type_fn = system_type_fn;
					valid_double = system_solve_response(value_buf,1024,&number,NULL,json_object_get_string(val));
				}

				if(unlikely(strlen(value_buf)==0))
				{
					rdlog(LOG_WARNING,"Not seeing %s value.", name);
				}
				else if(!splittok)
				{
					if(valid_double)
					{
						process_novector_monitor(
							worker_info,
							sensor_data,
							libmatheval_variables,
							name, value_buf, number,
							valueslist, unit,
							group_name, group_id,
							type_fn,
							sensor_data->enrichment,
							send,integer);
					}
					else
					{
						rdlog(LOG_WARNING,"Value '%s' of '%s' is not a number",value_buf,key);
					}
				}
				else /* We have a vector here */
				{
					process_vector_monitor(worker_info,
						sensor_data,
						libmatheval_variables,
						name,value_buf, splittok,
						valueslist, unit, group_id,
						group_name, instance_prefix,
						name_split_suffix, splitop,
						sensor_data->enrichment, send,
						timestamp_given, type_fn,
						&memctx,integer);
				}

				if(nonzero && rd_dz(number))
				{
					rdlog(LOG_ALERT,"value oid=%s is 0, but nonzero setted. skipping.",json_object_get_string(val));
					bad_names[bad_names_pos++] = name;
					send = 0;
				}
			}else if(0==strncmp(key,"op",strlen("op"))){ // @TODO sepparate in it's own function
				const char * operation = (char *)json_object_get_string(val);
				int op_ok=1;
				for(unsigned int i=0;op_ok==1 && i<bad_names_pos;++i)
				{
					if(strstr(bad_names[i],operation)){
						rdlog(LOG_NOTICE,"OP %s Uses a previously bad marked value variable (%s). Skipping",
							operation,bad_names[i]);
						send = op_ok = 0;
					}
				}

				if(op_ok)
				{
					struct vector_variables_s{ // @TODO Integrate in struct monitor_value
						char *name;size_t name_len;unsigned int pos;
						SIMPLEQ_ENTRY(vector_variables_s) entry;
					};
					SIMPLEQ_HEAD(listhead,vector_variables_s) head = SIMPLEQ_HEAD_INITIALIZER(head);

					char * str_op_variables = strdup(operation); /* TODO: use libmatheval_names better */
					char * str_op = NULL;
					char *auxchar;
					char * tok = strtok_r(str_op_variables,OPERATIONS,&auxchar);

					SIMPLEQ_INIT(&head);
					size_t vectors_len=1;
					size_t vector_variables_count = 0;
					while(tok) /* searching if some variable is a vector */
					{
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
					monitor_value.sensor_id = sensor_data->sensor_id;
					monitor_value.sensor_name = sensor_data->sensor_name;
					monitor_value.instance_prefix = instance_prefix;
					monitor_value.bad_value = 0;
					monitor_value.unit=unit;
					monitor_value.group_name=group_name;
					monitor_value.group_id=group_id;
					monitor_value.type = op_type;
					monitor_value.enrichment = sensor_data->enrichment;

					for(size_t j=0;op_ok && j<vectors_len;++j) /* foreach member of vector */
					{
						const size_t namelen = strlen(name); // @TODO make the suffix append in libmatheval_append
						char * mathname = rd_memctx_calloc(&memctx,namelen+strlen(VECTOR_SEP)+6,sizeof(char)); /* space enough to save _60000 */
						strcpy(mathname,name);
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
							rdlog(LOG_DEBUG,"Result of operation %s: %lf",key,number);
							if(rd_dz(number) == 0 && nonzero)
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
								char * vector_pos = NULL;
								sprintf(name_buf,"%s%s",name,name_split_suffix);
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
								monitor_value.name = name;
								monitor_value.value=number;
								monitor_value.instance_valid = 0;
								monitor_value.string_value=val_buf;
							}
							const struct monitor_value * new_mv = update_monitor_value(worker_info->monitor_values_tree,&monitor_value);

							if ((send) && new_mv)
								rd_lru_push(valueslist, (void *)new_mv);
						}
						if(op_ok && splitop){
							sum+=number;
							count++;
						}
						mathname=NULL;

					} /* foreach member of vector */
					if(splitop)
					{
						if(0==strcmp(splitop,"sum"))
							number = sum;
						else if(0==strcmp(splitop,"mean"))
							number = sum/count;
						else
							op_ok=0;

						if(op_ok){
							char split_op_result[64];
							sprintf(split_op_result,"%lf",number);
							monitor_value.name = name;
							monitor_value.value=number;
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
			}

			if(errno!=0)
			{
				rdlog(LOG_ERR,"Could not parse %s value: %s",key,strerror(errno));
			}
		} /* foreach monitor attribute */

		const struct monitor_value * monitor_value = NULL;
		while((monitor_value = rd_lru_pop(valueslist))) {
			struct printbuf* printbuf= print_monitor_value(monitor_value);
			if(likely(NULL!=printbuf)){
				if(likely(send && sensor_data->peername
					&& sensor_data->sensor_name
					&& sensor_data->community))
				{
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
	} /* foreach monitor parameter */
	destroy_snmp_session(snmp_sessp);

	if(aok)
	{
		rd_memctx_freeall(&memctx);
		delete_libmatheval_stuffs(libmatheval_variables);
	}
	return aok;
}

/** Parse a JSON child with a provided callback
  @param base Base object
  @param child_key Child's key
  @param cb Callback to use against c-json child object
  @return child parsed with cb, or default value
  */
#define PARSE_CJSON_CHILD0(base, child_key, cb, default_value) ({              \
		json_object *value = NULL;                                     \
		json_object_object_get_ex(base, child_key, &value);            \
		value ? cb(value) : default_value;                             \
	})

/// Convenience macro to parse a int64 child
#define PARSE_CJSON_CHILD_INT64(base, child_key, default_value)                \
	PARSE_CJSON_CHILD0(base, child_key, json_object_get_int64,             \
		default_value)

/// Convenience function to get a string chuld duplicated
static char *json_object_get_dup_string(json_object *json) {
	const char *ret = json_object_get_string(json);
	return ret ? strdup(ret) : NULL;
}

/// Convenience macro to get a string chuld duplicated
#define PARSE_CJSON_CHILD_STR(base, child_key, default_value)                  \
	PARSE_CJSON_CHILD0(base, child_key, json_object_get_dup_string,        \
		default_value)

/** Fill sensor information
  @param sensor Sensor to store information
  @param sensor_info JSON describing sensor
  */
static void sensor_common_attrs_parse_json(rb_sensor_t *sensor,
					/* const */ json_object *sensor_info) {
	sensor->data.timeout = PARSE_CJSON_CHILD_INT64(sensor_info, "timeout",
						(int64_t)sensor->data.timeout);
	sensor->data.sensor_id = PARSE_CJSON_CHILD_INT64(sensor_info,
								"sensor_id", 0);
	sensor->data.sensor_name = PARSE_CJSON_CHILD_STR(sensor_info,
							"sensor_name", NULL);
	sensor->data.peername = PARSE_CJSON_CHILD_STR(sensor_info,
							"sensor_ip", NULL);
	sensor->data.community = PARSE_CJSON_CHILD_STR(sensor_info,
							"community", NULL);
	json_object_object_get_ex(sensor_info, "monitors", &sensor->monitors);
	if (sensor->monitors) {
		json_object_get(sensor->monitors);
	}
	json_object_object_get_ex(sensor_info, "enrichment",
						&sensor->data.enrichment);
	const char *snmp_version = PARSE_CJSON_CHILD_STR(sensor_info,
							"snmp_version", NULL);
	if (snmp_version) {
		sensor->data.snmp_version = net_snmp_version(snmp_version,
						sensor->data.sensor_name);
	}
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
	check_setted(sensor->data.peername,&aok,
		"[CONFIG] Peername not setted in sensor ", sensor_name);
	check_setted(sensor->data.community,&aok,
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
	sensor_common_attrs_parse_json(sensor, sensor_info);
	return sensor_common_attrs_check_sensor(sensor);
}

/** Sets sensor defaults
  @param worker_info Worker info that contains defaults
  @param sensor Sensor to store defaults
  */
static void sensor_set_defaults(const struct _worker_info *worker_info,
							rb_sensor_t *sensor) {
	sensor->data.timeout = worker_info->timeout;
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
bool process_rb_sensor(struct _worker_info * worker_info, rb_sensor_t *sensor,
								rd_lru_t *ret) {
	return process_sensor_monitors(worker_info, &sensor->data,
							sensor->monitors, ret);
}

/** Free allocated memory for sensor
  @param sensor Sensor to free
  */
static void sensor_done(rb_sensor_t *sensor) {
	free(sensor->data.peername);
	free(sensor->data.sensor_name);
	free(sensor->data.community);
	json_object_put(sensor->monitors);
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

/*
 * SENSORS ARRAY
 */

/** Create a new array with vsize capacity */
struct rb_sensor_array *rb_sensors_array_new(size_t vsize) {
	struct rb_sensor_array *ret = calloc(1, sizeof(*ret)
						+ vsize*sizeof(*ret->sensors));
	if (ret) {
		ret->size = vsize;
	}
	return ret;
}

/** Destroy a sensors array */
void rb_sensors_array_done(struct rb_sensor_array *array) {
	free(array);
}
