#include <stdlib.h>
#include <stdio.h>
#include <json/json.h>
#include <json/printbuf.h>
#include <unistd.h>
#include <syslog.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <librd/rdqueue.h>
#include <librd/rdthread.h>
#include <librd/rdlru.h>
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <librdkafka/rdkafka.h>
#include <math.h>
#include <matheval.h>

#ifndef NDEBUG
#include <ctype.h>
#endif

#include "rb_stringlist.h"

#define START_SPLITTEDTOKS_SIZE 64 /* @TODO: test for low values, forcing reallocation */
#define SPLITOP_RESULT_LEN 512     /* String that will save a double */
#define MAX_VECTOR_OP_VARIABLES 64 /* Maximum variables in a vector operation */
                                   /* @TODO make dynamic */
                                   /* @TODO test low values */

const char * OPERATIONS = "+-*/";

#define DEBUG_STDOUT 0x1
#define DEBUG_SYSLOG 0x2

void swap(void **a,void **b){
	void * temp=*b;*b=*a; *a=temp;
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
    "\"max_kafka_fails\": 2,"
    "\"sleep_main\": 10,"
    "\"sleep_worker\": 2,"
    "\"kafka_broker\": \"localhost\","
    "\"kafka_topic\": \"SNMP\","
    #if RD_KAFKA_VERSION<0x00080000
    "\"kafka_start_partition\": 0,"
    "\"kafka_end_partition\": 2"
    #endif
  "}";

/// SHARED Info needed by threads.
struct _worker_info{
	struct snmp_session default_session;
	pthread_mutex_t snmp_session_mutex;
	const char * community,*kafka_broker,*kafka_topic;
	const char * max_kafka_fails; /* I want a const char * because rd_kafka_conf_set implementation */
	int64_t sleep_worker,max_snmp_fails,timeout,debug,debug_output_flags;
	#if RD_KAFKA_VERSION == 0x00080000
	int64_t kafka_timeout;
	#else
	int64_t kafka_start_partition,kafka_current_partition,kafka_end_partition;
	#endif
	rd_fifoq_t *queue;
};

struct _sensor_data{
	int timeout;
	const char * peername; 
	const char * sensor_name;
	uint64_t sensor_id;
	const char * community;
};

struct libmatheval_stuffs{
	const char ** names;
	double *values;
	unsigned int variables_pos;
	unsigned int total_lenght;
};

struct _perthread_worker_info{
	rd_kafka_t * rk;
	#if RD_KAFKA_VERSION == 0x00080000
	rd_kafka_topic_t *rkt;
	#endif
	int thread_ok;
	struct _sensor_data sensor_data;
	struct libmatheval_stuffs libmatheval_variables;
};

struct _main_info{
	const char * syslog_indent;
	int64_t sleep_main,threads;
};

int run = 1;
void sigproc(int sig) {
  static int called = 0;

  if(called++) return;
  run = 0;
  (void)sig;
}

#if 0
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */
#endif


/** @WARNING worker_info->syslog_session_mutex locked and unlocked in this call! 
    be aware: performance, race conditions...
    */
static inline void Log(const struct _worker_info *worker_info, const int level,char *fmt,...){
	assert(worker_info);

	if(level >= worker_info->debug)
		return;

	if(worker_info->debug_output_flags != 0){
		va_list ap;
		if(worker_info->debug_output_flags & DEBUG_STDOUT)
		{
			va_start(ap, fmt);
			switch(level){
				case LOG_EMERG:
				case LOG_ALERT:
				case LOG_CRIT:
				case LOG_ERR:
				case LOG_WARNING:
	    			vfprintf(stderr, fmt, ap);
	    			break;					
				case LOG_NOTICE:
				case LOG_INFO:
				case LOG_DEBUG:
					vfprintf(stdout, fmt, ap);
					break;
			};
			va_end(ap);
		}

		if(worker_info->debug_output_flags & DEBUG_SYSLOG)
		{
			va_start(ap, fmt);
			vsyslog(level, fmt, ap);
		    va_end(ap);
		}
	}
}

static int libmatheval_append(struct _worker_info *worker_info, struct libmatheval_stuffs *matheval,const char *name,double val){
	Log(worker_info,LOG_DEBUG,"[libmatheval] Saving %s var in libmatheval array. Value=%.3lf\n",
						                                         name,val);

	if(matheval->variables_pos<matheval->total_lenght){
		assert(worker_info);
		assert(matheval);
		assert(matheval->names);
		assert(matheval->values);
	}else{
		if (NULL != (matheval->names = realloc(matheval->names,matheval->total_lenght*2*sizeof(char *)))
			&& NULL != (matheval->values = realloc(matheval->values,matheval->total_lenght*2*sizeof(double))))
		{
			matheval->total_lenght*=2;
		}
		else
		{
			Log(worker_info,LOG_CRIT,"Memory error. \n",__LINE__);
			if(matheval->names) free(matheval->names);
			matheval->total_lenght = 0;
			return 0;
		}
	}

	matheval->names[matheval->variables_pos] = name;
	matheval->values[matheval->variables_pos++] = val;
	return 1;
}

static int libmatheval_search_vector(const char ** variables,size_t variables_count, const char *vector, size_t *pos,size_t *size)
{
	int state = 0; /* 0: searching; 1: counting length; 2:finished */
	int ret = 0;
	*pos =0;
	*size=0;
	const size_t strlen_vector = strlen(vector);
	for(unsigned int i=0;state<2 && i<variables_count;++i)
	{
		switch(state)
		{
			case 0:
				if(strncmp(variables[i],vector,strlen_vector)==0)
				{
					state++;
					(*size)++;
					(*pos) = i;
					ret=1;
				}
				break;
			case 1:
				if(strncmp(variables[i],vector,strlen_vector)==0)
					(*size)++;
				else
					state++;
				break;
			default:
				break;
		};
	}
	return ret;
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

json_bool parse_json_config(json_object * config,struct _worker_info *worker_info,
	                                          struct _main_info *main_info){
	
	int ret = TRUE;
	json_object_object_foreach(config, key, val){
		errno = 0;
		if(0==strncmp(key,"debug",sizeof "debug"-1))
		{
			worker_info->debug = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"stdout",sizeof "stdout"-1))
		{
			if(json_object_get_int64(val)) 
				worker_info->debug_output_flags |= DEBUG_STDOUT;
			else                           
				worker_info->debug_output_flags &= ~DEBUG_STDOUT;
		}
		else if(0==strncmp(key,"syslog",sizeof "syslog"-1))
		{
			if(json_object_get_int64(val)) 
				worker_info->debug_output_flags |= DEBUG_SYSLOG;
			else
				worker_info->debug_output_flags &= ~DEBUG_SYSLOG;
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
#if RD_KAFKA_VERSION == 0x00080000
		else if(0==strncmp(key,"kafka_start_partition", strlen("kafka_start_partition"))
			 || 0==strncmp(key,"kafka_end_partition", strlen("kafka_end_partition")))
		{
			Log(worker_info,LOG_WARNING,"%s Can only be specified in kafka 0.7. Skipping",key);
		}
		else if(0==strncmp(key,"kafka_timeout", strlen("kafka_timeout")))
		{
			worker_info->kafka_timeout = json_object_get_int64(val);
		}
#else
		else if(0==strncmp(key,"kafka_start_partition", strlen("kafka_start_partition")))
		{
			worker_info->kafka_start_partition = worker_info->kafka_end_partition = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"kafka_end_partition", strlen("kafka_end_partition")))
		{
			worker_info->kafka_end_partition = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"kafka_timeout", strlen("kafka_timeout")))
		{
			Log("%s Can only be specified in kafka 0.8. Skipping",key);
		}
#endif
		else if(0==strncmp(key,"sleep_worker",sizeof "sleep_worker"-1))
		{
			worker_info->sleep_worker = json_object_get_int64(val);
		}else{
			Log(worker_info,LOG_ERR,"Don't know what config.%s key means.\n",key);
		}
		
		if(errno!=0)
		{
			Log(worker_info,LOG_ERR,"Could not parse %s value: %s",key,strerror(errno));
			ret=FALSE;
		}
	}
	return ret;
}

#if RD_KAFKA_VERSION == 0x00080000 && !defined(NDEBUG)
/**
 * Message delivery report callback.
 * Called once for each message.
 * See rdkafka.h for more information.
 */
static void msg_delivered (rd_kafka_t *rk,
			   void *payload, size_t len,
			   int error_code,
			   void *opaque, void *msg_opaque) {
	(void)payload,(void)msg_opaque;
	if (error_code)
		Log(opaque,LOG_DEBUG,"%% Message delivery failed: %s\n",
		       rd_kafka_err2str(rk, error_code));
	else
		Log(opaque,LOG_DEBUG,"%% Message delivered (%zd bytes)\n", len);
}

#endif

static inline void check_setted(const void *ptr,struct _worker_info *worker_info,
	int *aok,const char *errmsg,const char *sensor_name)
{
	assert(aok);
	if(*aok && ptr == NULL){
		*aok = 0;
		Log(worker_info,LOG_ERR,"%s%s",errmsg,sensor_name?sensor_name:"(some sensor)\n");
	}
}

/** 
	Adapt the snmp response in string and double responses.
	@param value_buf  Return buffer where the response will be saved (text format)
	@param number     If possible, the response will be saved in double format here
	@param response   SNMP response
	@return           0 if number was not setted; non 0 otherwise.
 */
static inline int snmp_solve_response(const struct _worker_info *worker_info, 
	char * value_buf,const size_t value_buf_len,double * number,struct snmp_pdu *response)
{
	/* A lot of variables. Just if we pass SNMPV3 someday.
	struct variable_list *vars;
	for(vars=response->variables; vars; vars=vars->next_variable)
		print_variable(vars->name,vars->name_length,vars);
	*/
	int ret = 0;
	assert(value_buf);
	assert(number);
	assert(response);
	
	//Log(worker_info,LOG_DEBUG,"response type: %d\n",response->variables->type);

	switch(response->variables->type) // See in /usr/include/net-snmp/types.h
	{ 
		case ASN_INTEGER:
			snprintf(value_buf,sizeof(value_buf),"%ld",*response->variables->val.integer);
			*number = *response->variables->val.integer;
			ret = 1;		
			break;
		case ASN_OCTET_STR:
			/* We don't know if it's a double inside a string; We try to convert and save */
			strncpy(value_buf,(const char *)response->variables->val.string,value_buf_len);
			// @TODO check val_len before copy string.
			value_buf[response->variables->val_len] = '\0';
			*number = strtod((const char *)response->variables->val.string,NULL);
			ret = 1;
			break;
		default:
			Log(worker_info,LOG_WARNING,"Unknow variable type %d in SNMP response. Line %d\n",response->variables->type,__LINE__);
	};

	return ret;

}

/** @note our way to save a SNMP string vector in libmatheval_array is:
    names:  [ ...  vector_name_0 ... vector_name_(N)     vector_name   ... ]
    values: [ ...     number0    ...    number(N)      split_op_result ... ]
*/

/* @warning This function assumes ALL fields of sensor_data will be populated */
int process_sensor_monitors(struct _worker_info *worker_info,struct _perthread_worker_info *pt_worker_info,
			json_object * monitors)
{
	int aok=1;
	struct _sensor_data *sensor_data = &pt_worker_info->sensor_data;
	struct libmatheval_stuffs *libmatheval_variables = &pt_worker_info->libmatheval_variables;
	size_t libmatheval_start_pos;
	void * sessp=NULL;
	struct snmp_session *ss;

	libmatheval_variables->variables_pos = 0;
	const char *bad_names[json_object_array_length(monitors)]; /* ops cannot be added to libmatheval array.*/
	size_t bad_names_pos = 0;

	assert(sensor_data->sensor_name);
	check_setted(sensor_data->peername,worker_info,&aok,
		"Peername not setted in %s. Skipping.\n",sensor_data->sensor_name);
	check_setted(sensor_data->community,worker_info,&aok,
		"Community not setted in %s. Skipping.\n",sensor_data->sensor_name);

	if(libmatheval_variables->names == NULL) /* starting allocate */
	{
		const size_t new_size = json_object_array_length(monitors)*10; /* Allocating enough memory */
		libmatheval_variables->names = 
			calloc(new_size,sizeof(char *));
		if(NULL==libmatheval_variables->names)
		{
			Log(worker_info,LOG_CRIT,"Cannot allocate memory. Exiting.\n");
			aok = 0;
		}
		else
		{
			libmatheval_variables->values =
				calloc(new_size,sizeof(double));
			if(NULL==libmatheval_variables->values)
			{
				Log(worker_info,LOG_CRIT,"Cannot allocate memory. Exiting.\n");
				aok = 0;
			}
			else
			{
				libmatheval_variables->total_lenght = new_size;
			}
		}
	}

	if(aok){
		pthread_mutex_lock(&worker_info->snmp_session_mutex);
		/* @TODO: You can do it later, see session_api.h */
		worker_info->default_session.peername = (char *)sensor_data->peername;
		sessp = snmp_sess_open(&worker_info->default_session);
		if(NULL== sessp || NULL == (ss = snmp_sess_session(sessp))){
			Log(worker_info,LOG_ERR,"Error creating session: %s",snmp_errstring(worker_info->default_session.s_snmp_errno));
			aok=0;
		}else{
			/*memcpy(ss,&worker_info->default_session,sizeof(*ss)); Much pointers!*/
			/*if (ss->community) free(ss->community);*/
			ss->community = (u_char *)strdup(sensor_data->community);
			ss->community_len = strlen(sensor_data->community);
			/*if (ss->peername) free(ss->peername);*/
			ss->timeout = sensor_data->timeout;

			ss->flags = worker_info->default_session.flags;
		}
		pthread_mutex_unlock(&worker_info->snmp_session_mutex);
	}

	for(int i=0; aok && run && i<json_object_array_length(monitors);++i){
		struct timeval tv;
		gettimeofday(&tv,NULL);

		const char * name=NULL,*name_split_suffix = NULL,*instance_prefix=NULL;
		json_object *monitor_parameters_array = json_object_array_get_idx(monitors, i);
		int kafka=0,nonzero=0;
		const char * splittok=NULL,*splitop=NULL;
		char *split_op_result=NULL;
		const char * unit=NULL;
		double number; // <- @TODO merge with split_op_result */
		int number_setted = 0;

		
		rd_lru_t * valueslist    = rd_lru_new();
		rd_lru_t * instance_list = rd_lru_new();
		rd_lru_t * gc_tofree     = rd_lru_new(); // garbage collector
		
		
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
			}else if(0==strcmp(key2,"nonzero")){
				nonzero = 1;
			}else if(0==strncmp(key2,"kafka",strlen("kafka")) || 0==strncmp(key2,"name",strlen("name"))){
				kafka = 1;
			}else if(0==strncmp(key2,"oid",strlen("oid")) || 0==strncmp(key2,"op",strlen("op"))){
				// will be resolved in the next foreach
			}else{
				Log(worker_info,LOG_ERR,"Cannot parse %s argument (line %d)\n",key2,__LINE__);
			}

			if(errno!=0){
				Log(worker_info,LOG_ERR,"Could not parse %s value: %s",key2,strerror(errno));
			}

		} /* foreach */

		if(unlikely(NULL==sensor_data->sensor_name)){
			Log(worker_info,LOG_ERR,"sensor name not setted. Skipping.\n");
			break;
		}

		json_object_object_foreach(monitor_parameters_array,key,val){
			errno=0;
			if(0==strncmp(key,"split",strlen("split")+1)){
				// already resolved
			}else if(0==strcmp(key,"split_op")){
				// already resolved
			}else if(0==strcmp(key,"name")){ 
				// already resolved
			}else if(0==strcmp(key,"name_split_suffix")){
				// already resolved
			}else if(0==strcmp(key,"instance_prefix")){
				// already resolved
			}else if(0==strncmp(key,"unit",strlen("unit"))){
				// already resolved
			}else if(0==strcmp(key,"nonzero")){
				// already resolved
			}else if(0==strncmp(key,"kafka",strlen("kafka")) || 0==strncmp(key,"name",strlen("name"))){
				// already resolved
			}else if(0==strncmp(key,"oid",strlen("oid"))){
				/* @TODO extract in it's own SNMPget function */
				/* @TODO test passing a sensor without params to caller function. */
				if(unlikely(!name)){
					Log(worker_info,LOG_WARNING,"name of param not set in %s. Skipping\n",
						sensor_data->sensor_name);
					break /*foreach*/;
				}

				char * value_buf = calloc(1024,sizeof(char)); /* @TODO make flexible */
				struct snmp_pdu *pdu=snmp_pdu_create(SNMP_MSG_GET);
				struct snmp_pdu *response=NULL;
				oid entry_oid[MAX_OID_LEN];
				size_t entry_oid_len = MAX_OID_LEN;
				read_objid(json_object_get_string(val),entry_oid,&entry_oid_len);
				snmp_add_null_var(pdu,entry_oid,entry_oid_len);
				int status = snmp_sess_synch_response(sessp,pdu,&response);
				if(likely(status==STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR))
				{ // @TODO refactor all this block. Search for repeated code.
					number_setted = snmp_solve_response(worker_info,value_buf,1024,&number,response);
					Log(worker_info,LOG_DEBUG,"SNMP OID %s response: %s\n",json_object_get_string(val),value_buf);
					if(!splittok)
					{
						if(likely(libmatheval_append(worker_info,libmatheval_variables,name,number)))
						{
							if(kafka)
								rd_lru_push(valueslist,value_buf);
							else
								free(value_buf);
						}
						else
						{
							Log(worker_info,LOG_ERR,"Error adding libmatheval value\n");
							aok = 0;
							free(value_buf);
							value_buf=NULL;
						}
					}
					else
					{ /* adding suffix if vector. @TODO: implement in libmatheval_append? One char* suffix param? */
						char * tok = value_buf;
						unsigned int count = 0,mean_count=0;
						double sum=0;
						libmatheval_start_pos = libmatheval_variables->variables_pos;
						const size_t name_len = strlen(name);
						while(tok){
							char * nexttok = strstr(tok,splittok);
							if(nexttok != '\0')
							{
								*nexttok = '\0';
								nexttok++;
							}
							if(*tok)
							{
								char * tok_name = calloc(name_len+7,sizeof(char)); /* +7: space to allocate _65535 */
								rd_lru_push(gc_tofree,tok_name);
								strcpy(tok_name,name);
								snprintf(tok_name+name_len,7,"_%u",count);
								if(likely(0!=libmatheval_append(worker_info,libmatheval_variables,tok_name,atof(tok))))
								{
									if(kafka)
									{
										char * cptoken = malloc(sizeof(char)*strlen(tok));
										strcpy(cptoken,tok);
										rd_lru_push(valueslist,cptoken);
										rd_lru_push(instance_list,(void *)(unsigned long)count);
									}
								}
								else
								{
									Log(worker_info,LOG_ERR,"Error adding libmatheval value\n");
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
								Log(worker_info,LOG_WARNING,"Not seeing value %d\n",count);
							}

							tok = nexttok;
							count++;
						} /* while(tok) */
						free(value_buf); /* Don't needed anymore */
						value_buf=0;

						// Last token reached. Do we have an operation to do?
						if(NULL!=splitop){
							if((split_op_result = calloc(SPLITOP_RESULT_LEN,sizeof(char))))
							{
								double result = 0;
								int splitop_is_valid = 1;
								if(0==strcmp("sum",splitop)){
									result = sum;
								}else if(0==strcmp("mean",splitop)){
									result = sum/mean_count;
								}
								
								if(splitop_is_valid)
								{
									snprintf(split_op_result,SPLITOP_RESULT_LEN,"%lf",sum);
									if(0==libmatheval_append(worker_info,libmatheval_variables,name,result))
									{
										Log(worker_info,LOG_WARNING,"Cannot save %s -> %s in libmatheval_variables.\n",
											name,split_op_result);
									}
								}
								else
								{
									Log(worker_info,LOG_WARNING,"Splitop %s unknow in monitor parameter %s\n",splitop,name);
									free(split_op_result);
									split_op_result=NULL;
								}
							}
							else
							{
								Log(worker_info,LOG_CRIT,"Memory error.\n");
								break; /* foreach */
							}
						}
					}

					if(nonzero && 0 == number)
					{
						Log(worker_info,LOG_ALERT,"value oid=%s is 0, but nonzero setted. skipping.\n");
						bad_names[bad_names_pos++] = name;
						kafka=0;
					}
					snmp_free_pdu(response);
				}
				else
				{
					if (status == STAT_SUCCESS)
						Log(worker_info,LOG_ERR,"Error in packet.Reason: %s\n",snmp_errstring(response->errstat));
					else
						Log(worker_info,LOG_ERR,"Snmp error: %s\n", snmp_api_errstring(ss->s_snmp_errno));
				}
			}else if(0==strncmp(key,"op",strlen("op"))){
				const char * operation = (char *)json_object_get_string(val);
				int op_ok=1;
				for(unsigned int i=0;op_ok==1 && i<bad_names_pos;++i)
				{
					if(strstr(bad_names[i],operation)){
						Log(worker_info,LOG_NOTICE,"OP %s Uses a previously bad marked value variable (%s). Skipping\n",
							operation,bad_names[i]);
						kafka=op_ok=0;
					}
				}
				if(op_ok)
				{
					struct vector_variables_s{ // @TODO extract in it's own file.
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
								Log(worker_info,LOG_ERR,"vector dimension mismatch\n");
								op_ok=0;
							}
						}
						tok = strtok_r(NULL,OPERATIONS,&auxchar);
					}

					if(vector_variables_count>0)
					{
						/* +6: to add _99999 to all variables */
						str_op = calloc(strlen(operation)+6*vector_variables_count,sizeof(char)); // @TODO more descriptive name
					}
					else
					{
						str_op = (char *)operation;
					}

					double sum=0;unsigned count=0;
					for(size_t j=0;op_ok && j<vectors_len;++j) /* foreach member of vector */
					{
						const size_t namelen = strlen(name); // @TODO make the suffix append in libmatheval_append
						char * mathname = calloc(namelen+6,sizeof(char)); /* space enough to save _60000 */
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
						Log(worker_info,LOG_DEBUG,"vector operation string result: %s\n",str_op);

						/* extracting suffix */
						if(vectors_len>1)
						{
							const int mathpos = SIMPLEQ_FIRST(&head)->pos + j;
							const char * suffix = strrchr(libmatheval_variables->names[mathpos],'_');
							strcpy(mathname+namelen,suffix);
						}
						
						

						void * const f = evaluator_create ((char *)str_op); /* really it has to do (void *). See libmatheval doc. */
					                                                       /* also, we have to create a new one for each iteration */
						number = evaluator_evaluate (f, libmatheval_variables->variables_pos,
							(char **) libmatheval_variables->names,	libmatheval_variables->values);
						evaluator_destroy (f);
						number_setted = 1;
						Log(worker_info,LOG_DEBUG,"Result of operation %s: %lf\n",key,number);
						if(number == 0 && nonzero)
							Log(worker_info,LOG_ERR,"OP %s return 0, and nonzero setted. Skipping.\n",operation);
						if(number == INFINITY|| number == -INFINITY|| number == NAN)
							Log(worker_info,LOG_ERR,"OP %s return a bad value: %lf. Skipping.\n",operation,number);
						/* op will send by default, so we ignore kafka param */
						if(libmatheval_append(worker_info,&pt_worker_info->libmatheval_variables, mathname,number))
						{
							char *buf = calloc(64,sizeof(char));
							sprintf(buf,"%lf",number);
							rd_lru_push(valueslist,buf);								
							const unsigned long suffix_l = atol(strrchr(mathname,'_') + 1);
							rd_lru_push(instance_list,(void *)(unsigned long)suffix_l);
						}
						if(splitop){
							sum+=number;
							count++;
						}
						free(mathname);

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
							split_op_result = calloc(64,sizeof(char));
							sprintf(split_op_result,"%lf",number);
						}
					}

					struct vector_variables_s *vector_iterator,*tmp_vector_iterator;
					SIMPLEQ_FOREACH_SAFE(vector_iterator,tmp_vector_iterator,&head,entry)
					{
						free(vector_iterator);
					}
					if(vector_variables_count>0) // @TODO: make it more simple
						free(str_op);
					free(str_op_variables);
				}
			}else{
				Log(worker_info,LOG_ERR,"Cannot parse %s argument (line %d)\n",key,__LINE__);
			}

			if(errno!=0)
			{
				Log(worker_info,LOG_ERR,"Could not parse %s value: %s",key,strerror(errno));
			}
		} /* foreach */

		if(kafka){
			char * vector_value = NULL; 
			while((vector_value = rd_lru_pop(valueslist)) || split_op_result)
			{
				struct printbuf* printbuf= printbuf_new();
				if(likely(NULL!=printbuf)){
					// @TODO use printbuf_memappend_fast instead! */
					sprintbuf(printbuf, "{");
					sprintbuf(printbuf, "\"timestamp\":%lu,",tv.tv_sec);
					sprintbuf(printbuf, "\"sensor_id\":%lu,",sensor_data->sensor_id);
					sprintbuf(printbuf, "\"sensor_name\":\"%s\",",sensor_data->sensor_name);
					if(splitop && name_split_suffix && vector_value) // @TODO order this if/else/if, they are the same approx
						sprintbuf(printbuf, "\"monitor\":\"%s%s\",",name,name_split_suffix);
					else
						sprintbuf(printbuf, "\"monitor\":\"%s\",",name);
					if(splittok && instance_prefix && vector_value)
						sprintbuf(printbuf, "\"instance\":\"%s%lu\",",instance_prefix,(unsigned long)rd_lru_pop(instance_list));
					sprintbuf(printbuf, "\"type\":\"monitor\",");
					if(vector_value){
						sprintbuf(printbuf, "\"value\":\"%s\",", vector_value);
						free(vector_value);
					}else{
						sprintbuf(printbuf, "\"value\":\"%s\",", split_op_result);
						free(split_op_result);
						split_op_result = NULL;
					}
					sprintbuf(printbuf, "\"unit\":\"%s\"", unit);
					sprintbuf(printbuf, "}");

					//char * str_to_kafka = printbuf->buf;
					//printbuf->buf=NULL;
					if(likely(sensor_data->peername && sensor_data->sensor_name && sensor_data->community)){
						#if RD_KAFKA_VERSION == 0x00080000
						Log(worker_info,LOG_DEBUG,"[Kafka:random] %s\n",printbuf->buf);
						#else
						Log(worker_info,LOG_DEBUG,"[Kafka:%d] %s\n",worker_info->kafka_current_partition,printbuf->buf);
						#endif

						#if RD_KAFKA_VERSION == 0x00080000

						#ifndef NDEBUG
						int i=0;
						for(i=0;printbuf && i<printbuf->bpos;++i)
							assert(isprint(printbuf->buf[i] ));
						#endif
							
						if(likely(0==rd_kafka_produce(pt_worker_info->rkt, RD_KAFKA_PARTITION_UA,
								RD_KAFKA_MSG_F_FREE,
								/* Payload and length */
								printbuf->buf, printbuf->bpos,
								/* Optional key and its length */
								NULL, 0,
								/* Message opaque, provided in
								 * delivery report callback as
								 * msg_opaque. */
								NULL))){
							printbuf->buf=NULL; // rdkafka will free it
						}

						#else /* KAFKA_07 */

						if(likely(0==rd_kafka_produce(
							pt_worker_info->rk, (char *)worker_info->kafka_topic, 
								worker_info->kafka_current_partition++,
								RD_KAFKA_OP_F_FREE,printbuf->buf, printbuf->bpos))){
							printbuf->buf=NULL; // rdkafka will free it
						}

						#endif

						else
						{
							Log(worker_info,LOG_ERR,"[Errkafka] Cannot produce kafka message\n");
						}
					}
					printbuf_free(printbuf);
					#ifndef RD_KAFKA_VERSION // RD_KAFKA_VERSION < 0x00080000
					if(worker_info->kafka_current_partition>worker_info->kafka_end_partition)
						worker_info->kafka_current_partition = worker_info->kafka_start_partition;
					#endif
					printbuf = NULL;
				}else{ /* if(printbuf) after malloc */
					Log(worker_info,LOG_ALERT,"Cannot allocate memory for printbuf. Skipping\n");
				}
			} /* for i in splittoks */

			#if RD_KAFKA_VERSION >= 0x00080000 && !defined(NDEBUG)
			rd_kafka_poll(pt_worker_info->rk, worker_info->kafka_timeout); /* Check for callbacks */
			#endif /* RD_KAFKA_VERSION */


		} /* if kafka */

		free(split_op_result);
		rd_lru_destroy(valueslist);
		rd_lru_destroy(instance_list);
		void * garbage;
		while((garbage = rd_lru_pop(gc_tofree)))
			free(garbage);
		rd_lru_destroy(gc_tofree);
	}
	snmp_sess_close(sessp);

	return aok;
} 

int process_sensor(struct _worker_info * worker_info,struct _perthread_worker_info *pt_worker_info,json_object *sensor_info){
	memset(&pt_worker_info->sensor_data,0,sizeof(pt_worker_info->sensor_data));
	pt_worker_info->libmatheval_variables.variables_pos = 0;
	pt_worker_info->sensor_data.timeout = worker_info->timeout;
	json_object * monitors = NULL;
	int aok = 1;

	json_object_object_foreach(sensor_info, key, val){
		if(0==strncmp(key,"timeout",strlen("timeout"))){
			pt_worker_info->sensor_data.timeout = json_object_get_int64(val)*1e6; /* convert ms -> s */
		}else if (0==strncmp(key,"sensor_name",strlen("sensor_name"))){
			pt_worker_info->sensor_data.sensor_name = json_object_get_string(val);
		}else if (0==strncmp(key,"sensor_id",strlen("sensor_id"))){
			pt_worker_info->sensor_data.sensor_id = json_object_get_int64(val);
		}else if (0==strncmp(key,"sensor_ip",strlen("sensor_ip"))){
			pt_worker_info->sensor_data.peername = json_object_get_string(val);
		}else if(0==strncmp(key,"community",sizeof "community"-1)){
			pt_worker_info->sensor_data.community = json_object_get_string(val);
		}else if(0==strncmp(key,"monitors", strlen("monitors"))){
			monitors = val;
		}else {
			Log(worker_info,LOG_ERR,"Cannot parse %s argument\n",key);
		}
	}


	check_setted(pt_worker_info->sensor_data.sensor_name,worker_info,&aok,
		"[CONFIG] Sensor_name not setted in ",NULL);
	check_setted(pt_worker_info->sensor_data.peername,worker_info,&aok,
		"[CONFIG] Peername not setted in sensor ",pt_worker_info->sensor_data.sensor_name);
	check_setted(pt_worker_info->sensor_data.community,worker_info,&aok,
		"[CONFIG] Community not setted in sensor ",pt_worker_info->sensor_data.sensor_name);
	check_setted(monitors,worker_info,&aok,
		"[CONFIG] Monitors not setted in sensor ",pt_worker_info->sensor_data.sensor_name);
	
	if(aok)
		aok = process_sensor_monitors(worker_info, pt_worker_info, monitors);

	return aok;
}


void * worker(void *_info){
	struct _worker_info * worker_info = (struct _worker_info*)_info;
	struct _perthread_worker_info pt_worker_info;
	#if RD_KAFKA_VERSION == 0x00080000
	rd_kafka_conf_t conf;
	rd_kafka_topic_conf_t topic_conf;
	char errstr[256];
	#endif	
	pt_worker_info.thread_ok = 1;
	unsigned int msg_left,prev_msg_left=0,throw_msg_count;

	memset(&pt_worker_info.libmatheval_variables,0,sizeof(pt_worker_info.libmatheval_variables));
	

	#if RD_KAFKA_VERSION == 0x00080000

	rd_kafka_defaultconf_set(&conf);
	#if !defined(NDEBUG)
	conf.opaque = worker_info; /* Change msg_delivered function if you change this! */
	conf.producer.dr_cb = msg_delivered;
	#endif

	rd_kafka_topic_defaultconf_set(&topic_conf);

	const rd_kafka_conf_res_t confset = 
		rd_kafka_conf_set (&conf,"message.send.max.retries",worker_info->max_kafka_fails,
		errstr, sizeof errstr);

	switch(confset)
	{
		case RD_KAFKA_CONF_UNKNOWN: /* Unknown configuration name. */
			Log(worker_info,LOG_EMERG,"Error in line %d, invalid name. FIX\n",__LINE__);
			break;
		case RD_KAFKA_CONF_INVALID:
			Log(worker_info,LOG_ERR,"Error in configuration file. Value not valid in max_kafka_fails.\n");
			break;
		case RD_KAFKA_CONF_OK:
			break; /* AOK */
	}

	if (!(pt_worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER, &conf,errstr, sizeof(errstr)))) {
		Log(worker_info,LOG_ERR,"Error calling kafka_new producer: %s\n",errstr);
		pt_worker_info.thread_ok=0;
	}

	if(pt_worker_info.thread_ok && worker_info->debug_output_flags | DEBUG_SYSLOG)
		rd_kafka_set_logger(pt_worker_info.rk,rd_kafka_log_syslog);

	if (rd_kafka_brokers_add(pt_worker_info.rk, worker_info->kafka_broker) == 0) {
		Log(worker_info,LOG_ERR,"No valid brokers specified\n");
		pt_worker_info.thread_ok=0;
	}
	pt_worker_info.rkt = rd_kafka_topic_new(pt_worker_info.rk, worker_info->kafka_topic, &topic_conf);
	rd_kafka_topic_defaultconf_set(&topic_conf);

	#else /* KAFKA_08 */

	if (!(pt_worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER, worker_info->kafka_broker, NULL))) {
		Log(worker_info,LOG_ERR,"Error calling kafka_new producer: %s\n",strerror(errno));
		pt_worker_info.thread_ok=0;
	}
	#endif

	Log(worker_info,LOG_INFO,"Thread %lu connected successfuly\n.",pthread_self());
	while(pt_worker_info.thread_ok && run){
		rd_fifoq_elm_t * elm;
		while((elm = rd_fifoq_pop_timedwait(worker_info->queue,1)) && run){
			Log(worker_info,LOG_DEBUG,"Pop element %p\n",elm->rfqe_ptr);
			json_object * sensor_info = elm->rfqe_ptr;
			process_sensor(worker_info,&pt_worker_info,sensor_info);
			rd_fifoq_elm_release(worker_info->queue,elm);
		}
		sleep(worker_info->sleep_worker);
	}

	free(pt_worker_info.libmatheval_variables.names);
	free(pt_worker_info.libmatheval_variables.values);

	throw_msg_count = atoi(worker_info->max_kafka_fails);
	while(throw_msg_count && (msg_left = rd_kafka_outq_len (pt_worker_info.rk) ))
	{
		if(prev_msg_left == msg_left) /* Send no messages in a second? probably, the broker has fall down */
			throw_msg_count--;
		else
			throw_msg_count = atoi(worker_info->max_kafka_fails);
		Log(worker_info,LOG_INFO,
			"[Thread %u] Waiting for messages to send. Still %u messages to be exported. %u retries left.\n",
			pthread_self(),msg_left,throw_msg_count);
		prev_msg_left = msg_left;
		#ifndef NDEBUG
		rd_kafka_poll(pt_worker_info.rk,1);
		#endif
		sleep(worker_info->timeout/1000 + 1);
	}

	rd_kafka_topic_destroy(pt_worker_info.rkt);
	rd_kafka_destroy(pt_worker_info.rk);

	return _info; // just avoiding warning.
}

void queueSensors(struct json_object * sensors,rd_fifoq_t *queue,struct _worker_info *worker_info){
	for(int i=0;i<json_object_array_length(sensors);++i){
		json_object *value = json_object_array_get_idx(sensors, i);
		Log(worker_info,LOG_DEBUG,"Push element %p\n",value);
		rd_fifoq_add(queue,value);
	}
}

int main(int argc, char  *argv[])
{
	char *configPath=NULL;
	char opt;
	struct json_object * config_file=NULL,*config=NULL,*sensors=NULL;
	struct json_object * default_config = json_tokener_parse( str_default_config );
	struct _worker_info worker_info;
	struct _main_info main_info = {0};
	memset(&worker_info,0,sizeof(worker_info));
	assert(default_config);
	pthread_t * pd_thread = NULL;
	rd_fifoq_t queue = {{0}};
	json_bool ret;

	assert(default_config);
	ret = parse_json_config(default_config,&worker_info,&main_info);
	assert(ret==TRUE);

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
			worker_info.debug = atoi(optarg);
			break;
		default:
			printHelp(argv[0]);
			exit(1);
		}
	}

	if(worker_info.debug >=0)
		MC_SET_DEBUG(1);

	if(configPath == NULL){
		Log(&worker_info,LOG_ERR,"Config path not set. Exiting\n");
		exit(1);
	}

	config_file = json_object_from_file(configPath);
	if(!config_file){
		Log(&worker_info,LOG_CRIT,"[EE] Could not open config file %s. Exiting\n",configPath);
		exit(1);
	}

	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);


	if(FALSE==json_object_object_get_ex(config_file,"conf",&config)){
		Log(&worker_info,LOG_WARNING,"Could not fetch \"conf\" object from config file. Using default config instead.");
	}else{
		assert(NULL!=config);
		parse_json_config(config,&worker_info,&main_info); // overwrite some or all default values.
	}

	snmp_sess_init(&worker_info.default_session); /* set defaults */
	worker_info.default_session.version  = SNMP_VERSION_1;
	pthread_mutex_init(&worker_info.snmp_session_mutex,0);
	main_info.syslog_indent = "rb_monitor";
	openlog(main_info.syslog_indent, 0, LOG_USER);

	if(FALSE==json_object_object_get_ex(config_file,"sensors",&sensors)){
		Log(&worker_info,LOG_CRIT,"[EE] Could not fetch \"sensors\" array from config file. Exiting\n");
	}else{
		rd_fifoq_init(&queue);
		init_snmp("redBorder-monitor");
		pd_thread = malloc(sizeof(pthread_t)*main_info.threads);
		if(NULL==pd_thread){
			Log(&worker_info,LOG_CRIT,"[EE] Unable to allocate threads memory. Exiting.\n");
		}else{
			Log(&worker_info,LOG_INFO,"Main thread started successfuly. Starting workers threads.\n");
			worker_info.queue = &queue;
			for(int i=0;i<main_info.threads;++i){
				pthread_create(&pd_thread[i], NULL, worker, (void*)&worker_info);
			}

			while(run){
				queueSensors(sensors,&queue,&worker_info);
				sleep(main_info.sleep_main);
			}
			Log(&worker_info,LOG_INFO,"Leaving, wait 1sec for workers...\n");

			for(int i=0;i<main_info.threads;++i){
				pthread_join(pd_thread[i], NULL);
			}
			free(pd_thread);
		}
	}

	pthread_mutex_destroy(&worker_info.snmp_session_mutex);
	json_object_put(default_config);
	json_object_put(config_file);
	rd_fifoq_destroy(&queue);
	closelog();
	return ret;
}
