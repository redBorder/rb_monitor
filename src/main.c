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

#include "config.h"

#include "rb_sensor.h"
#include "rb_system.h"
#include "rb_libmatheval.h"
#include "rb_snmp.h"
#include "rb_values_list.h"

#ifdef HAVE_ZOOKEEPER
#include "rb_monitor_zk.h"
#endif

#ifdef HAVE_RBHTTP
#include <librbhttp/rb_http_handler.h>
#define RB_HTTP_NORMAL_MODE  0
#define RB_HTTP_CHUNKED_MODE 1
#endif

#include <math.h>
#include <librd/rdfloat.h>
#include <librd/rdlog.h>
#include <stdlib.h>
#include <json/json.h>
#include <json/printbuf.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <librd/rdqueue.h>
#include <librd/rdthread.h>
#include <librd/rdmem.h>
#include <librd/rdlru.h>
#include <librdkafka/rdkafka.h>
#include <math.h>
#include <matheval.h>

#ifndef NDEBUG
#include <ctype.h>
#endif

#define START_SPLITTEDTOKS_SIZE 64 /* @TODO: test for low values, forcing reallocation */
#define SPLITOP_RESULT_LEN 512     /* String that will save a double */
#define MAX_VECTOR_OP_VARIABLES 64 /* Maximum variables in a vector operation */
                                   /* @TODO make dynamic */
                                   /* @TODO test low values */

#define VECTOR_SEP "_pos_"
#define GROUP_SEP  "_gid_"

#define CONFIG_RDKAFKA_KEY "rdkafka."
#define CONFIG_ZOOKEEPER_KEY "zookeeper"

#define  ENABLE_RBHTTP_CONFIGURE_OPT "--enable-rbhttp"

const char * OPERATIONS = "+-*/&^|";

static inline void swap(void **a,void **b){
	void * temp=*b;*b=*a; *a=temp;
}

/* do strtod conversion plus set errno=EINVAL if no possible conversion */
double toDouble(const char * str)
{
	char * endPtr;
	errno=0;
	double d = strtod(str,&endPtr);
	if(errno==0 && endPtr==str)
		errno = EINVAL;
	return d;
}



#ifndef SIMPLEQ_FOREACH_SAFE
/*
* SIMPLEQ_FOREACH_SAFE() provides a traversal where the current iterated element
* may be freed or unlinked.
* It does not allow freeing or modifying any other element in the list,
* at least not the next element.
*/
#define SIMPLEQ_FOREACH_SAFE(elm,tmpelm,head,field)   \
for ((elm) = SIMPLEQ_FIRST(head) ;                    \
(elm) && ((tmpelm) = SIMPLEQ_NEXT((elm), field), 1) ; \
(elm) = (tmpelm))
#endif

/// Fallback config in json format
const char * str_default_config = /* "conf:" */ "{"
    "\"debug\": 100,"
    "\"syslog\":0,"
    "\"stdout\":1,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_snmp_fails\": 2,"
    "\"sleep_main\": 10,"
    "\"sleep_worker\": 2,"
  "}";

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

struct _sensor_data{
	int timeout;
	const char * peername;
	json_object * enrichment;
	const char * sensor_name;
	uint64_t sensor_id;
	const char * community;
	long snmp_version;
};

struct _main_info{
	const char * syslog_indent;
	int64_t sleep_main,threads;
#ifdef HAVE_ZOOKEEPER
	struct rb_monitor_zk *zk;
#endif
};

int run = 1;
void sigproc(int sig) {
  static int called = 0;

  if(called++) return;
  run = 0;
  (void)sig;
}

void printHelp(const char * progName){
	fprintf(stderr,
		"Usage: %s [-c path/to/config/file] [-g] [-v]"
		"\n"
		" Options:\n"
		"  -g           Go Daemon\n"
		"  -c <config>  Path to configuration file\n"
		"  -d           Debug\n"
		"\n"
		" This program will fetch SNMP data and re-send it to a Apache kafka broker.\n"
		" See config file for details."
		"\n",
		progName);
}

static void parse_rdkafka_keyval_config(rd_kafka_conf_t *rk_conf,rd_kafka_topic_conf_t *rkt_conf,
													const char *key,const char *value){
	// Extracted from Magnus Edenhill's kafkacat
	rd_kafka_conf_res_t res;
	char errstr[512];

	const char *name = key + strlen(CONFIG_RDKAFKA_KEY);

	res = RD_KAFKA_CONF_UNKNOWN;
	/* Try "topic." prefixed properties on topic
	 * conf first, and then fall through to global if
	 * it didnt match a topic configuration property. */
	if (!strncmp(name, "topic.", strlen("topic.")))
		res = rd_kafka_topic_conf_set(rkt_conf,
					      name+strlen("topic."),
					      value,errstr,sizeof(errstr));

	if (res == RD_KAFKA_CONF_UNKNOWN)
		res = rd_kafka_conf_set(rk_conf, name, value,
					errstr, sizeof(errstr));

	if (res != RD_KAFKA_CONF_OK)
		rdlog(LOG_ERR,"rdkafka: %s", errstr);
}

static void parse_rdkafka_config_json(struct _worker_info *worker_info,
							const char *key,json_object *jvalue){
	const char *value = json_object_get_string(jvalue);
	parse_rdkafka_keyval_config(worker_info->rk_conf,worker_info->rkt_conf,key,value);
}

#ifdef HAVE_ZOOKEEPER
static void parse_zookeeper_json(struct _main_info *main_info, struct _worker_info *worker_info,json_object *zk_config) {
	char *host = NULL;
	uint64_t pop_watcher_timeout = 0,push_timeout = 0;
	json_object *zk_sensors = NULL;

	json_object_object_foreach(zk_config, key, val) {
		if(0 == strcmp(key, "host")) {
			host = strdup(json_object_get_string(val));
		} else if(0 == strcmp(key, "pop_watcher_timeout")) {
			pop_watcher_timeout = json_object_get_int64(val);
		} else if(0 == strcmp(key, "push_timeout")) {
			push_timeout = json_object_get_int64(val);
		} else if(0 == strcmp(key, "sensors")) {
			zk_sensors = val;
		} else {
			rdlog(LOG_ERR,"Don't know what zookeeper config.%s key means.",key);
		}

		if(errno!=0)
		{
			rdlog(LOG_ERR,"Could not parse %s value: %s",key,strerror(errno));
			return;
		}
	}

	if(NULL == host) {
		rdlog(LOG_ERR,"No zookeeper host specified. Can't use ZK.");
		return;
	} else if (0 == push_timeout) {
		rdlog(LOG_INFO,"No pop push_timeout specified. We will never be ZK masters.");
		return;
	}

	main_info->zk = init_rbmon_zk(host,pop_watcher_timeout,
		push_timeout,zk_sensors,worker_info->queue);
}
#endif


json_bool parse_json_config(json_object * config,struct _worker_info *worker_info,
	                                          struct _main_info *main_info){

	int ret = TRUE;
	json_object_object_foreach(config, key, val){
		errno = 0;
		if(0==strncmp(key,"debug",sizeof "debug"-1))
		{
			rd_log_set_severity(json_object_get_int64(val));
		}
		else if(0==strncmp(key,"stdout",sizeof "stdout"-1))
		{
#if 0
			/// @TODO recover
			if(json_object_get_int64(val))
				worker_info->debug_output_flags |= DEBUG_STDOUT;
			else
				worker_info->debug_output_flags &= ~DEBUG_STDOUT;
#endif
		}
		else if(0==strncmp(key,"syslog",sizeof "syslog"-1))
		{
#if 0
			/// @TODO recover
			if(json_object_get_int64(val))
				worker_info->debug_output_flags |= DEBUG_SYSLOG;
			else
				worker_info->debug_output_flags &= ~DEBUG_SYSLOG;
#endif
		}
		else if(0==strncmp(key,"threads",sizeof "threads"-1))
		{
			main_info->threads = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"timeout",sizeof "timeout"-1))
		{
			worker_info->timeout = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"max_snmp_fails",sizeof "max_snmp_fails"-1))
		{
			worker_info->max_snmp_fails = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"max_kafka_fails",sizeof "max_kafka_fails"-1))
		{
			worker_info->max_kafka_fails = json_object_get_string(val);
		}
		else if(0==strncmp(key,"sleep_main",sizeof "sleep_main"-1))
		{
			main_info->sleep_main = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"kafka_broker", strlen("kafka_broker")))
		{
			worker_info->kafka_broker	= json_object_get_string(val);
		}
		else if(0==strncmp(key,"kafka_topic", strlen("kafka_topic")))
		{
			worker_info->kafka_topic	= json_object_get_string(val);
		}
		else if(0==strncmp(key,"kafka_start_partition", strlen("kafka_start_partition"))
			 || 0==strncmp(key,"kafka_end_partition", strlen("kafka_end_partition")))
		{
			rdlog(LOG_WARNING,"%s Can only be specified in kafka 0.7. Skipping",key);
		}
		else if(0==strncmp(key,"kafka_timeout", strlen("kafka_timeout")))
		{
			worker_info->kafka_timeout = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"sleep_worker",sizeof "sleep_worker"-1))
		{
			worker_info->sleep_worker = json_object_get_int64(val);
		}
		else if (0 == strncmp(key, "http_endpoint", strlen("http_endpoint")))
		{
#ifdef HAVE_RBHTTP
			worker_info->http_endpoint	= json_object_get_string(val);
#else
			rdlog(LOG_ERR,"rb_monitor does not have librbhttp support, so %s key is invalid. Please compile it with %s",
					key,ENABLE_RBHTTP_CONFIGURE_OPT);
#endif
		}
		else if (0==strncmp(key, CONFIG_RDKAFKA_KEY, strlen(CONFIG_RDKAFKA_KEY)))
		{
			parse_rdkafka_config_json(worker_info,key,val);
		}
		else if(0==strncmp(key,"http_max_total_connections",sizeof "http_max_total_connections"-1))
		{
			worker_info->http_max_total_connections = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"http_timeout",sizeof "http_timeout"-1))
		{
			worker_info->http_timeout = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"http_connttimeout",sizeof "http_connttimeout"-1))
		{
			worker_info->http_connttimeout = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"http_verbose",sizeof "http_verbose"-1))
		{
			worker_info->http_verbose = json_object_get_int64(val);
		}
		else if(0==strcmp(key,"http_insecure"))
		{
#ifdef HAVE_RBHTTP
			worker_info->http_insecure = json_object_get_int64(val);
#else
			rdlog(LOG_ERR,"rb_monitor does not have librbhttp support, so %s key is invalid. Please compile it with %s",
					key,ENABLE_RBHTTP_CONFIGURE_OPT);
#endif
		}
		else if(0==strncmp(key,"rb_http_max_messages",sizeof "rb_http_max_messages"-1))
		{
			worker_info->rb_http_max_messages = json_object_get_int64(val);
		}
		else if(0==strcmp(key,"rb_http_mode"))
		{
#ifdef HAVE_RBHTTP
			const char *sval = json_object_get_string(val);
			if(NULL == sval)
			{
				rdlog(LOG_ERR,"Invalid rb_http_mode");
			}
			else if (0==strcmp(sval,"normal"))
			{
				worker_info->http_mode = RB_HTTP_NORMAL_MODE;
			}
			else if (0 == strcmp(sval,"deflated"))
			{
				worker_info->http_mode = CHUNKED_MODE;
			}
			else
			{
				rdlog(LOG_ERR,"Invalid rb_http_mode %s",sval);
			}
#else
			rdlog(LOG_ERR,"rb_monitor does not have librbhttp support, so %s key is invalid. Please compile it with %s",
					key,ENABLE_RBHTTP_CONFIGURE_OPT);
#endif
		}
		else
		{
			rdlog(LOG_ERR,"Don't know what config.%s key means.",key);
		}

		if(errno!=0)
		{
			rdlog(LOG_ERR,"Could not parse %s value: %s",key,strerror(errno));
			ret=FALSE;
		}
	}
	return ret;
}

/**
 * Message delivery report callback.
 * Called once for each message.
 * See rdkafka.h for more information.
 */
static void msg_delivered (rd_kafka_t *rk,
			   void *payload, size_t len,
			   int error_code,
			   void *opaque, void *msg_opaque) {
	(void)rk,(void)opaque,(void)payload,(void)msg_opaque;
	if (error_code)
		rdlog(LOG_ERR,"%% Message delivery failed: %s",
		       rd_kafka_err2str(error_code));
	else
		rdlog(LOG_DEBUG,"%% Message delivered (%zd bytes)", len);
}

static inline void check_setted(const void *ptr,int *aok,const char *errmsg,const char *sensor_name)
{
	assert(aok);
	if(*aok && ptr == NULL){
		*aok = 0;
		rdlog(LOG_ERR,"%s%s",errmsg,sensor_name?sensor_name:"(some sensor)");
	}
}

/** @note our way to save a SNMP string vector in libmatheval_array is:
    names:  [ ...  vector_name_0 ... vector_name_(N)     vector_name   ... ]
    values: [ ...     number0    ...    number(N)      split_op_result ... ]
*/

// @todo pass just a monitor_value with all precached possible.
int process_novector_monitor(struct _worker_info *worker_info,struct _sensor_data * sensor_data, struct libmatheval_stuffs* libmatheval_variables,
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
int process_vector_monitor(struct _worker_info *worker_info,struct _sensor_data * sensor_data, struct libmatheval_stuffs* libmatheval_variables,
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
int process_sensor_monitors(struct _worker_info *worker_info,
		struct _sensor_data *sensor_data, json_object *monitors,
		rd_lru_t *ret) {
	int aok=1;
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

	for(int i=0; aok && run && i<json_object_array_length(monitors);++i){
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

int process_sensor(struct _worker_info * worker_info,
				json_object *sensor_info, rd_lru_t *ret) {
	struct _sensor_data sensor_data;
	memset(&sensor_data,0,sizeof(sensor_data));
	sensor_data.timeout = worker_info->timeout;
	json_object * monitors = NULL;
	int aok = 1;

	json_object_object_foreach(sensor_info, key, val){
		if(0==strncmp(key,"timeout",strlen("timeout"))){
			sensor_data.timeout = json_object_get_int64(val)*1e6; /* convert ms -> s */
		}else if (0==strncmp(key,"sensor_name",strlen("sensor_name"))){
			sensor_data.sensor_name = json_object_get_string(val);
		}else if (0==strncmp(key,"sensor_id",strlen("sensor_id"))){
			sensor_data.sensor_id = json_object_get_int64(val);
		}else if (0==strncmp(key,"sensor_ip",strlen("sensor_ip"))){
			sensor_data.peername = json_object_get_string(val);
		}else if(0==strncmp(key,"community",strlen("community"))){
			sensor_data.community = json_object_get_string(val);
		}else if(0==strncmp(key,"snmp_version",strlen("snmp_version"))){
			const char *string_version = json_object_get_string(val);
			sensor_data.snmp_version = net_snmp_version(string_version,sensor_data.sensor_name);
		}else if(0==strncmp(key,"monitors", strlen("monitors"))){
			monitors = val;
		}else if(0==strncmp(key,"enrichment", strlen("enrichment"))){
			sensor_data.enrichment = val;
		}else {
			rdlog(LOG_ERR,"Cannot parse %s argument",key);
			aok=0;
		}
	}

	const char *sensor_name = sensor_data.sensor_name;

	check_setted(sensor_data.sensor_name,&aok,
		"[CONFIG] Sensor_name not setted in ",NULL);
	check_setted(sensor_data.peername,&aok,
		"[CONFIG] Peername not setted in sensor ",sensor_name);
	check_setted(sensor_data.community,&aok,
		"[CONFIG] Community not setted in sensor ",sensor_name);
	check_setted(monitors,&aok,
		"[CONFIG] Monitors not setted in sensor ",sensor_name);

	if(aok) {
		aok = process_sensor_monitors(worker_info, &sensor_data,
								monitors, ret);
	}

	return aok;
}

#ifdef HAVE_RBHTTP
static void msg_callback(struct rb_http_handler_s *rb_http_handler, int status_code,
                         long http_status, const char *status_code_str, char *buff,
                         size_t bufsiz, void *opaque) {

        if (status_code != 0) {
            rdlog(LOG_ERR,"Curl returned %d code for one message: %s\n", status_code, status_code_str);
        }

        if (status_code == 0 && http_status != 200) {
            rdlog(LOG_ERR,"HTTP server returned %ld STATUS.\n", http_status);
        }

        (void) rb_http_handler;
        (void) bufsiz;
        (void) status_code_str;
        (void) buff;
        (void) opaque;
}

void *get_report_thread(void *http_handler) {

	while (rb_http_get_reports(http_handler, msg_callback, 100) || run);

	return NULL;
}
#endif

static int worker_process_sensor_send_message(struct _worker_info * worker_info,
							char *msg) {
	if(worker_info->kafka_broker != NULL) {
		rdlog(LOG_DEBUG,"[Kafka] %s\n",msg);
		const int produce_rc =  rd_kafka_produce(
			worker_info->rkt, RD_KAFKA_PARTITION_UA,
			RD_KAFKA_MSG_F_COPY,
			/* Payload and length */
			/// @TODO cache len
			msg, strlen(msg),
			/* Optional key and its length */
			NULL, 0,
			/* Message opaque, provided in delivery report
			 * callback as
			 * msg_opaque. */
			NULL);
		if (0!=produce_rc) {
			rdlog(LOG_ERR,
				"[Kafka] Cannot produce kafka message: %s",
				rd_kafka_err2str(rd_kafka_errno2err(errno)));
		}
	} /* if kafka */

#ifdef HAVE_RBHTTP
	if (worker_info->http_handler != NULL) {
		char err[BUFSIZ];
		rdlog(LOG_DEBUG,"[HTTP] %s\n",msg);
		const int produce_rc = rb_http_produce(
			worker_info->http_handler, msg, strlen(msg),
			RB_HTTP_MESSAGE_F_COPY, err, sizeof(err), NULL);
		if (0!=produce_rc) {
			rdlog(LOG_ERR, "[HTTP] Cannot produce message: %s",
									err);
		}
	}
#endif
	free(msg);
	return 0;
}

static int worker_process_sensor_send_messages(struct _worker_info *worker_info,
					rd_lru_t *msgs) {
	char *msg = NULL;
	while((msg = rd_lru_pop(msgs))) {
		worker_process_sensor_send_message(worker_info, msg);
	}

	return 0;
}

static int worker_process_sensor(struct _worker_info * worker_info,
				struct rb_sensor *sensor) {
	rd_lru_t *messages = rd_lru_new(); /// @TODO not use an lru!

	assert(sensor);
#ifdef RB_SENSOR_MAGIC
	assert(RB_SENSOR_MAGIC == sensor->magic);
#endif


	json_object * sensor_info = sensor->json_sensor;
	process_sensor(worker_info, sensor_info, messages);
	if(sensor->flags & RB_SENSOR_F_FREE) {
		json_object_put(sensor->json_sensor);
	}
	free(sensor);

	worker_process_sensor_send_messages(worker_info, messages);

	rd_lru_destroy(messages);

	return 0;
}

void * worker(void *_info){
	struct _worker_info *worker_info = _info;

	rdlog(LOG_INFO,"Thread %lu connected successfuly\n.",pthread_self());
	while(run){
		rd_fifoq_elm_t * elm;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
		while((elm = rd_fifoq_pop_timedwait(worker_info->queue,100)) && run){
			struct rb_sensor *sensor = elm->rfqe_ptr;
			rd_fifoq_elm_release(worker_info->queue,elm);
			rdlog(LOG_DEBUG,"Pop element %p from queue %p",
				sensor, worker_info->queue);
			worker_process_sensor(worker_info, sensor);
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	}

	return _info; // just avoiding warning.
}

void queueSensor(struct json_object *value,rd_fifoq_t *queue) {
	struct rb_sensor *sensor = calloc(1,sizeof(*sensor));
	if(!sensor) {
		rdlog(LOG_ERR,"Can't allocate sensor (out of memory?)");
	} else {
#ifdef RB_SENSOR_MAGIC
		sensor->magic = RB_SENSOR_MAGIC;
#endif
		sensor->json_sensor = value;
		rdlog(LOG_DEBUG,"Push element %p->%p",sensor,sensor->json_sensor);
		rd_fifoq_add(queue,sensor);
	}
}

void queueSensors(struct json_object * sensors,rd_fifoq_t *queue){
	for(int i=0;i<json_object_array_length(sensors);++i){
		json_object *value = json_object_array_get_idx(sensors, i);
		queueSensor(value,queue);
	}
}

static void *rdkafka_delivery_reports_poll_f(void * void_worker_info) {
	struct _worker_info *worker_info = void_worker_info;

	while(run) {
		rd_kafka_poll(worker_info->rk,500);
	}

	return NULL;
}

/// @TODO please delete this, is ugly
#ifndef UNDER_TEST

int main(int argc, char  *argv[])
{
	char *configPath=NULL;
	char opt;
	struct json_object * config_file=NULL,*config=NULL,*sensors=NULL,*zk=NULL;
	struct json_object * default_config = json_tokener_parse( str_default_config );
	struct _worker_info worker_info;
	struct _main_info main_info = {0};
	int debug_severity = LOG_INFO;
	pthread_t rdkafka_delivery_reports_poll_thread;

	memset(&worker_info,0,sizeof(worker_info));
	worker_info.rk_conf  = rd_kafka_conf_new();
	worker_info.rkt_conf = rd_kafka_topic_conf_new();

	assert(default_config);
	pthread_t * pd_thread = NULL;
	rd_fifoq_t queue;
	memset(&queue,0,sizeof(queue)); // Needed even with init()
	rd_fifoq_init(&queue);
	worker_info.queue = &queue;
	json_bool ret;

	assert(default_config);
	assert(NULL==strstr(VECTOR_SEP,OPERATIONS));
	ret = parse_json_config(default_config,&worker_info,&main_info);
	assert(ret==TRUE);
	worker_info.monitor_values_tree = new_monitor_values_tree();

	while ((opt = getopt(argc, argv, "gc:hvd:")) != -1) {
		switch (opt) {
		case 'h':
			printHelp(argv[0]);
			exit(0);
		case 'g':
			/* go_daemon = 1; */
			break;
		case 'c':
			configPath = optarg;
			break;
		case 'd':
			debug_severity = atoi(optarg);
			break;
		default:
			printHelp(argv[0]);
			exit(1);
		}
	}

	rd_log_set_severity(debug_severity);
	if(debug_severity >=0)
		MC_SET_DEBUG(1);

	if(configPath == NULL){
		rdlog(LOG_ERR,"Config path not set. Exiting");
		exit(1);
	}

	config_file = json_object_from_file(configPath);
	if(!config_file){
		rdlog(LOG_CRIT,"[EE] Could not open config file %s. Exiting",configPath);
		exit(1);
	}

	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);
	signal(SIGHUP, SIG_IGN);

	if(FALSE==json_object_object_get_ex(config_file,"conf",&config)){
		rdlog(LOG_WARNING,"Could not fetch \"conf\" object from config file. Using default config instead.");
	}else{
		assert(NULL!=config);
		parse_json_config(config,&worker_info,&main_info); // overwrite some or all default values.
	}

	if(FALSE != json_object_object_get_ex(config_file,"zookeeper",&zk)) {
#ifndef HAVE_ZOOKEEPER
			rdlog(LOG_ERR,"This monitor does not have zookeeper enabled.");
#else
			parse_zookeeper_json(&main_info,&worker_info,zk);
#endif
	}

	snmp_sess_init(&worker_info.default_session); /* set defaults */
	worker_info.default_session.version  = SNMP_VERSION_1;
	pthread_mutex_init(&worker_info.snmp_session_mutex,0);
	main_info.syslog_indent = "rb_monitor";
	openlog(main_info.syslog_indent, 0, LOG_USER);

	// rd_init();

	if (worker_info.kafka_broker!=NULL) {
		rd_kafka_conf_set_dr_cb(worker_info.rk_conf,msg_delivered);
		char errstr[BUFSIZ];
		if (!(worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER,
		                worker_info.rk_conf,errstr, sizeof(errstr)))) {
			rdlog(LOG_ERR,"Error calling kafka_new producer: %s\n",errstr);
			exit(1);
		}

		//if(worker_info->debug_output_flags | DEBUG_SYSLOG)
		//	rd_kafka_set_logger(worker_info.rk,rd_kafka_log_syslog);

		if (rd_kafka_brokers_add(worker_info.rk, worker_info.kafka_broker) == 0) {
			rdlog(LOG_ERR,"No valid brokers specified\n");
			exit(1);
		}
		worker_info.rkt = rd_kafka_topic_new(worker_info.rk, worker_info.kafka_topic,
			worker_info.rkt_conf);

		worker_info.rk_conf = NULL;
		worker_info.rkt_conf = NULL;

		pthread_create(&rdkafka_delivery_reports_poll_thread,NULL,
			rdkafka_delivery_reports_poll_f,&worker_info);
	} else {
		// Not needed
		rd_kafka_conf_destroy(worker_info.rk_conf);
		rd_kafka_topic_conf_destroy(worker_info.rkt_conf);
	}

#ifdef HAVE_RBHTTP
	if (worker_info.http_endpoint != NULL) {
		char err[BUFSIZ];
		worker_info.http_handler = rb_http_handler_create(worker_info.http_endpoint,
			err, sizeof(err));

		if(NULL == worker_info.http_handler) {
			rdlog(LOG_CRIT,"Couldn't create HTTP handler: %s",err);
			exit(1);
		}

		if (worker_info.http_endpoint != NULL) {
			char aux[BUFSIZ];
			char err[BUFSIZ];
			int rc = 0;

			char http_max_total_connections[sizeof("18446744073709551616")];
			char http_timeout[sizeof("18446744073709551616L")];
			char http_connttimeout[sizeof("18446744073709551616L")];
			char http_verbose[sizeof("18446744073709551616L")];
			char rb_http_max_messages[sizeof("18446744073709551616L")];

			snprintf(http_max_total_connections,sizeof(int64_t),"%"PRId64"",worker_info.http_max_total_connections);
			snprintf(http_timeout,sizeof(int64_t),"%"PRId64"",worker_info.http_timeout);
			snprintf(http_connttimeout,sizeof(int64_t),"%"PRId64"",worker_info.http_connttimeout);
			snprintf(http_verbose,sizeof(int64_t),"%"PRId64"",worker_info.http_verbose);
			snprintf(rb_http_max_messages,sizeof(int64_t),"%"PRId64"",worker_info.rb_http_max_messages);

			rb_http_handler_set_opt(worker_info.http_handler, "HTTP_MAX_TOTAL_CONNECTIONS",
						http_max_total_connections, err, sizeof(err));
			rb_http_handler_set_opt(worker_info.http_handler, "HTTP_TIMEOUT",
					http_timeout, err, sizeof(err));
			rb_http_handler_set_opt(worker_info.http_handler, "HTTP_CONNTTIMEOUT",
					http_connttimeout, err, sizeof(err));
			rb_http_handler_set_opt(worker_info.http_handler, "HTTP_VERBOSE",
					http_verbose, err, sizeof(err));
			rb_http_handler_set_opt(worker_info.http_handler, "RB_HTTP_MAX_MESSAGES",
					rb_http_max_messages, err, sizeof(err));

			snprintf(aux,sizeof(aux),"%"PRId64, worker_info.http_mode);

			rc = rb_http_handler_set_opt(worker_info.http_handler, "RB_HTTP_MODE",
				aux, err, sizeof(err));
			if (0 != rc) {
				rdlog(LOG_ERR,"Couldn't set RB_HTTP_MODE %"PRId64"(%s): %s",
					worker_info.http_mode, aux, err);
			}

			if (worker_info.http_insecure) {
				rb_http_handler_set_opt(worker_info.http_handler,
					"HTTP_INSECURE", "1", err, sizeof(err));
			}
		}

		rb_http_handler_run(worker_info.http_handler);

		if(pthread_create(&worker_info.pthread_report, NULL, get_report_thread,
		                worker_info.http_handler)) {
			fprintf(stderr, "Error creating thread\n");
		} else {
			rdlog(LOG_INFO, "[Thread] Created pthread_report thread. \n");
		}
	}
#endif /* HAVE_RBHTTP */

	if(FALSE==json_object_object_get_ex(config_file,"sensors",&sensors)){
		rdlog(LOG_CRIT,"[EE] Could not fetch \"sensors\" array from config file. Exiting");
	}else{
		init_snmp("redBorder-monitor");
		pd_thread = malloc(sizeof(pthread_t)*main_info.threads);
		if(NULL==pd_thread){
			rdlog(LOG_CRIT,"[EE] Unable to allocate threads memory. Exiting.");
		}else{
			rdlog(LOG_INFO,"Main thread started successfuly. Starting workers threads.");
			for(int i=0;i<main_info.threads;++i){
				pthread_create(&pd_thread[i], NULL, worker, (void*)&worker_info);
			}

			while(run){
				queueSensors(sensors,&queue);
				sleep(main_info.sleep_main);
			}
			rdlog(LOG_INFO,"Leaving, wait for workers...");

			for(int i=0;i<main_info.threads;++i){
				pthread_join(pd_thread[i], NULL);
			}
			free(pd_thread);
		}
	}

	if (worker_info.rk!=NULL) {
		int msg_left = 0;
		pthread_join(rdkafka_delivery_reports_poll_thread,NULL);
		while((msg_left = rd_kafka_outq_len (worker_info.rk) ))
		{
			rdlog(LOG_INFO,
				"Waiting for messages to send. Still %u messages to be exported.\n",
				msg_left);

			rd_kafka_poll(worker_info.rk,1000);
		}

		rd_kafka_topic_destroy(worker_info.rkt);
		rd_kafka_destroy(worker_info.rk);
		worker_info.rkt = NULL;
		worker_info.rk = NULL;
    }

#ifdef HAVE_RBHTTP
	if (worker_info.http_endpoint != NULL) {
		if(pthread_join(worker_info.pthread_report, NULL)) {
			fprintf(stderr, "Error joining thread\n");
			//return 2;

		}
		rdlog(LOG_INFO, "[Thread] pthread_report finishing. \n");
		rb_http_handler_destroy(worker_info.http_handler, NULL, 0);
	}
#endif

	pthread_mutex_destroy(&worker_info.snmp_session_mutex);
	json_object_put(default_config);
	json_object_put(config_file);
	rd_fifoq_destroy(&queue);
	closelog();

	return ret;
}

#endif /* UNDER_TEST */
