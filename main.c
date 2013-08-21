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
#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <librdkafka/rdkafka.h>
#include <matheval.h>

#ifndef NDEBUG
#include <ctype.h>
#endif

#define DEBUG_STDOUT 0x1
#define DEBUG_STDERR 0x2
#define DEBUG_SYSLOG 0x4

/// Fallback config in json format
const char * str_default_config = /* "conf:" */ "{"
    "\"debug\": 3,"
    "\"syslog\":0,"
    "\"stdout\":1,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_fails\": 2,"
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
	pthread_mutex_t session_mutex;
	const char * community,*kafka_broker,*kafka_topic;
	int64_t sleep_worker,max_fails,timeout,debug,debug_output_flags;
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

struct _perthread_worker_info{
	rd_kafka_t * rk;
	#if RD_KAFKA_VERSION == 0x00080000
	rd_kafka_topic_t *rkt;
	#endif
	int thread_ok;
	struct _sensor_data sensor_data;
};

struct _main_info{
	int64_t sleep_main,threads,debug,debug_output_flags;
};

int run = 1;
void sigproc(int sig) {
  static int called = 0;

  if(called++) return;
  run = 0;
  (void)sig;
}

static inline void Log(char *fmt,...){
	va_list ap;
	va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
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
		else if(0==strncmp(key,"threads",sizeof "threads"-1))
		{
			main_info->threads = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"timeout",sizeof "timeout"-1))
		{
			worker_info->timeout = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"max_fails",sizeof "max_fails"-1))
		{
			worker_info->max_fails = json_object_get_int64(val);
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
			Log("%s Can only be specified in kafka 0.7. Skipping",key);
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
			Log("[EE] Don't know what config.%s key means.\n",key);
		}
		
		if(errno!=0)
		{
			Log("[EE] Could not parse %s value: %s",key,strerror(errno));
			ret=FALSE;
		}
	}
	return ret;
}

struct libmatheval_stuffs{
	const char ** names;
	double *values;
	unsigned int variables_pos;
	unsigned int total_lenght;
};

static int libmatheval_append(struct libmatheval_stuffs *matheval,const char *name,double val){
	if(matheval->variables_pos<matheval->total_lenght){
		matheval->names[matheval->variables_pos] = name;
		matheval->values[matheval->variables_pos++] = val;
	}else{
		Log("[FIX] More variables than I can save in line %d. Have to fix\n",__LINE__);
		return 0;
	}
	return 1;
}

#if RD_KAFKA_VERSION == 0x00080000
/**
 * Message delivery report callback.
 * Called once for each message.
 * See rdkafka.h for more information.
 */
static void msg_delivered (rd_kafka_t *rk,
			   void __attribute((unused)) *payload, size_t len,
			   int error_code,
			   void __attribute((unused)) *opaque, void __attribute((unused)) *msg_opaque) {

	if (error_code)
		printf("%% Message delivery failed: %s\n",
		       rd_kafka_err2str(rk, error_code));
	else
		printf("%% Message delivered (%zd bytes)\n", len);
}

#endif

/* @warning This function assumes ALL fields of sensor_data will be populated */
int process_sensor_monitors(struct _worker_info *worker_info,struct _perthread_worker_info *pt_worker_info,
			json_object * monitors)
{
	int aok=1;
	struct _sensor_data *sensor_data = &pt_worker_info->sensor_data;
	void * sessp=NULL;
	struct snmp_session *ss;

	/* Just to avoid dynamic memory */
	const char *names[json_object_array_length(monitors)];
	double values[json_object_array_length(monitors)];
	
	struct libmatheval_stuffs matheval = {names,values,0,json_object_array_length(monitors)};

	assert(sensor_data->sensor_name);
	if(worker_info->debug>=1){
		if(!sensor_data->peername){
			Log("[EE] Peername not setted in %s. Skipping.\n",sensor_data->sensor_name);
			aok=0;
		}
		if(!sensor_data->community){
			Log("[EE] Community not setted in %s. Skipping.\n",sensor_data->sensor_name);
			aok=0;
		}
	}

	if(aok){
		pthread_mutex_lock(&worker_info->session_mutex);
		/* @TODO: You can do it later, see session_api.h */
		worker_info->default_session.peername = (char *)sensor_data->peername;
		sessp = snmp_sess_open(&worker_info->default_session);
		if(NULL== sessp || NULL == (ss = snmp_sess_session(sessp))){
			Log("Error creating session: %s",snmp_errstring(worker_info->default_session.s_snmp_errno));
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
		pthread_mutex_unlock(&worker_info->session_mutex);
	}

	for(int i=0; aok && run && i<json_object_array_length(monitors);++i){
		struct timeval tv;
		gettimeofday(&tv,NULL);

		const char * name=NULL,*name_split_suffix = NULL,*instance_prefix=NULL;
		json_object *monitor_parameters_array = json_object_array_get_idx(monitors, i);
		int kafka=0;
		const char * splittok=NULL,*splitop=NULL;
		struct printbuf* printbuf=NULL;
		const char * unit=NULL;
		char value_buf[1024];
		double number;
		int number_setted = 0;
		


		json_object_object_foreach(monitor_parameters_array,key,val){
			errno=0;
			if(0==strncmp(key,"split",strlen("split")+1)){
				splittok = json_object_get_string(val);
			}else if(0==strncmp(key,"split_op",strlen("split_op"))){
				splitop = json_object_get_string(val);
			}else if(0==strncmp(key,"name",strlen("name")+1)){ 
				name = json_object_get_string(val);
			}else if(0==strncmp(key,"name_split_suffix",strlen("name_split_suffix"))){
				name_split_suffix = json_object_get_string(val);
			}else if(0==strcmp(key,"instance_prefix")){
				instance_prefix = json_object_get_string(val);
			}else if(0==strncmp(key,"unit",strlen("unit"))){
				unit = json_object_get_string(val);
			}else if(0==strncmp(key,"kafka",strlen("kafka")) || 0==strncmp(key,"name",strlen("name"))){
				kafka = 1;
			}else if(0==strncmp(key,"oid",strlen("oid"))){

				/* @TODO test passing a sensor without params to caller function. */
				if(unlikely(!name)){
					if(worker_info->debug>=1)
						Log("[WW] name of param not set in %s. Skipping\n",sensor_data->sensor_name);
					break /*foreach*/;
				}

				struct snmp_pdu *pdu=snmp_pdu_create(SNMP_MSG_GET);
				struct snmp_pdu *response=NULL;
				oid entry_oid[MAX_OID_LEN];
				size_t entry_oid_len = MAX_OID_LEN;
				read_objid(json_object_get_string(val),entry_oid,&entry_oid_len);
				snmp_add_null_var(pdu,entry_oid,entry_oid_len);
				int status = snmp_sess_synch_response(sessp,pdu,&response);
				if(likely(status==STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR)){
					/* A lot of variables. Just if we pass SNMPV3 someday.
					struct variable_list *vars;
					for(vars=response->variables; vars; vars=vars->next_variable)
						print_variable(vars->name,vars->name_length,vars);
					*/
					
					double number;
					if(worker_info->debug>=4)
						Log("response type: %d\n",response->variables->type);

					switch(response->variables->type){ // See in /usr/include/net-snmp/types.h
						case ASN_INTEGER:
							snprintf(value_buf,sizeof(value_buf),"%ld",*response->variables->val.integer);
							if(worker_info->debug>=3){
								Log("Saving %s var in libmatheval array. OID=%s;Value=%d\n",
									name,json_object_get_string(val),*response->variables->val.integer);
							}
							libmatheval_append(&matheval,name,*response->variables->val.integer);
						
							break;
						case ASN_OCTET_STR:
							/* We don't know if it's a double inside a string; We try to convert and save */
							number = strtod((const char *)response->variables->val.string,NULL);
							number_setted = 1;
							if(splittok)
							{
								if(worker_info->debug>=3){
									Log("Saving %lf var in libmatheval array. OID=%s;Value=\"%lf\"\n",name,json_object_get_string(val),number);
								}
								libmatheval_append(&matheval,name,number);
							}

							// @TODO check val_len before copy string.
							strncpy(value_buf,(const char *)response->variables->val.string,sizeof(value_buf));
							value_buf[response->variables->val_len] = '\0';

							if(worker_info->debug>3){
								Log("SNMP response: %s\n",response->variables->val.string);
								Log("Saved SNMP response (added \\n): %s\n",value_buf);
							}

							break;
						default:
							Log("[WW] Unknow variable type %d in SNMP response. Line %d\n",response->variables->type,__LINE__);
					};

					snmp_free_pdu(response);

				}else if(worker_info->debug){
					if (status == STAT_SUCCESS)
						Log("Error in packet\nReason: %s\n",snmp_errstring(response->errstat));
					else
						Log("Snmp error: %s\n", snmp_api_errstring(ss->s_snmp_errno));
				}
			}else if(0==strncmp(key,"op",strlen("op"))){
				/* @TODO buffer this! */
				void *f = evaluator_create ((char *)json_object_get_string(val)); /*really it has to do (void *). See libmatheval doc. */
				number = evaluator_evaluate (f, matheval.variables_pos ,(char **) matheval.names,matheval.values);
				number_setted = 1;
				evaluator_destroy (f);
				if(worker_info->debug>=4)
					Log("Result of operation %s: %lf\n",key,number);
				/* op will send by default, so we ignore kafka param */
				libmatheval_append(&matheval, name,number);

			}else{
				if(worker_info->debug>=1)
					Log("Cannot parse %s argument (line %d)\n",key,__LINE__);
			}

			if(errno!=0)
			{
				Log("[EE] Could not parse %s value: %s",key,strerror(errno));
			}
		} /* foreach */

		if(unlikely(NULL==sensor_data->sensor_name)){
			Log("[WW] sensor name not setted. Skipping.\n");
			break;
		}

		if(kafka){
			char * saveptr=NULL;
			double sum=0;unsigned int count=0;int freetok=0;
			char * tok = splittok ? strtok_r(value_buf,splittok,&saveptr) : value_buf;
			                   /* at least one pass if splittok not setted */
			while(NULL!=tok)
			{
				printbuf = printbuf_new();
				if(likely(NULL!=printbuf)){
					// @TODO use printbuf_memappend_fast instead! */
					sprintbuf(printbuf, "{");
					sprintbuf(printbuf, "\"timestamp\":%lu,",tv.tv_sec);
					sprintbuf(printbuf, "\"sensor_id\":%lu,",sensor_data->sensor_id);
					sprintbuf(printbuf, "\"sensor_name\":\"%s\",",sensor_data->sensor_name);
					if(splittok && name_split_suffix)
						sprintbuf(printbuf, "\"monitor\":\"%s%s\",",name,name_split_suffix);
					else
						sprintbuf(printbuf, "\"monitor\":\"%s\",",name);
					if(splittok && instance_prefix)
						sprintbuf(printbuf, "\"instance\":\"%s%d\",",instance_prefix,count);
					sprintbuf(printbuf, "\"type\":\"monitor\",");
					sprintbuf(printbuf, "\"value\":\"%s\",", tok);
					sprintbuf(printbuf, "\"unit\":\"%s\"", unit);
					sprintbuf(printbuf, "}");

					//char * str_to_kafka = printbuf->buf;
					//printbuf->buf=NULL;
					if(likely(sensor_data->peername && sensor_data->sensor_name && sensor_data->community)){
						if(worker_info->debug>=3)
						{
							#if RD_KAFKA_VERSION == 0x00080000
							Log("[Kafka:random] %s\n",printbuf->buf);
							#else
							Log("[Kafka:%d] %s\n",worker_info->kafka_current_partition,printbuf->buf);
							#endif
						}
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
							Log("[Errkafka] Cannot produce kafka message\n");
						}

						if(NULL!=splitop)
							sum += atof(tok);
						count++; /* always count */

						if(freetok){
							free(tok);
							freetok=0;
						}

						tok = splittok ? strtok_r(NULL,splittok,&saveptr) : NULL;

						if(NULL==tok && NULL!=splitop){
							tok = calloc(1024,sizeof(char));
							if(0==strcmp("sum",splitop))
								snprintf(tok,1024,"%lf",sum);
							else if(0==strcmp("mean",splitop))
								snprintf(tok,1024,"%lf",sum/count);
							else if(worker_info->debug){
								Log("[WW] Splitop %s unknow in monitor parameter %s\n",splitop,name);
								free(tok);
								break; /* exit of while loop */
							}
							freetok=1;
							splittok=NULL; /* avoid strtok calls and re-enter in while after process tok */
							               /* avoid print per_instance_suffix too */
							splitop=NULL;  /* avoid enter in this block */
						}
					}
					printbuf_free(printbuf);
					#ifndef RD_KAFKA_VERSION // RD_KAFKA_VERSION < 0x00080000
					if(worker_info->kafka_current_partition>worker_info->kafka_end_partition)
						worker_info->kafka_current_partition = worker_info->kafka_start_partition;
					#endif
					printbuf = NULL;
				}else{
					Log("Cannot allocate memory for printbuf. Skipping\n");
				}
			} /* while tok */

			#if RD_KAFKA_VERSION == 0x00080000
			rd_kafka_poll(pt_worker_info->rk, worker_info->kafka_timeout);
			#endif /* RD_KAFKA_VERSION */

		} /* if kafka */

	}
	snmp_sess_close(sessp);

	return aok;
} 

static inline void process_sensor_check_setted(const void *ptr,int *aok,const int debug, const char *errmsg,const char *sensor_name){
	assert(aok);
	if(*aok && ptr == NULL){
			*aok = 0;
			if(debug>=1)
				Log("%s%s",errmsg,sensor_name?sensor_name:"(some sensor)\n");
	}
}

int process_sensor(struct _worker_info * worker_info,struct _perthread_worker_info *pt_worker_info,json_object *sensor_info){
	memset(&pt_worker_info->sensor_data,0,sizeof(pt_worker_info->sensor_data));
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
			if(worker_info->debug>=1)
				Log("Cannot parse %s argument\n",key);
		}
	}


	process_sensor_check_setted(pt_worker_info->sensor_data.sensor_name,&aok,worker_info->debug>=1,
		"[CONFIG] Sensor_name not setted in ",NULL);
	process_sensor_check_setted(pt_worker_info->sensor_data.peername,&aok,worker_info->debug,
		"[CONFIG] Peername not setted in sensor ",pt_worker_info->sensor_data.sensor_name);
	process_sensor_check_setted(pt_worker_info->sensor_data.community,&aok,worker_info->debug,
		"[CONFIG] Community not setted in sensor ",pt_worker_info->sensor_data.sensor_name);
	process_sensor_check_setted(monitors,&aok,worker_info->debug,
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
	

	#if RD_KAFKA_VERSION == 0x00080000

	rd_kafka_defaultconf_set(&conf);
	conf.producer.dr_cb = msg_delivered;

	rd_kafka_topic_defaultconf_set(&topic_conf);
	if (!(pt_worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER, &conf,errstr, sizeof(errstr)))) {
		if(worker_info->debug>=1)
			Log("Error calling kafka_new producer: %s\n",errstr);
		pt_worker_info.thread_ok=0;
	}

	if (rd_kafka_brokers_add(pt_worker_info.rk, worker_info->kafka_broker) == 0) {
		if(worker_info->debug>=1)
			Log("No valid brokers specified\n");
		pt_worker_info.thread_ok=0;
	}
	pt_worker_info.rkt = rd_kafka_topic_new(pt_worker_info.rk, worker_info->kafka_topic, &topic_conf);
	rd_kafka_topic_defaultconf_set(&topic_conf);

	#else /* KAFKA_08 */

	if (!(pt_worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER, worker_info->kafka_broker, NULL))) {
		if(worker_info->debug>=1)
			Log("Error calling kafka_new producer: %s\n",strerror(errno));
		pt_worker_info.thread_ok=0;
	}
	#endif
	
	while(pt_worker_info.thread_ok && run){
		rd_fifoq_elm_t * elm;
		while((elm = rd_fifoq_pop_timedwait(worker_info->queue,1)) && run){
			if(worker_info->debug>=2)
				Log("Pop element %p\n",elm->rfqe_ptr);
			json_object * sensor_info = elm->rfqe_ptr;
			process_sensor(worker_info,&pt_worker_info,sensor_info);
			rd_fifoq_elm_release(worker_info->queue,elm);
		}
		sleep(worker_info->sleep_worker);
	}


	return _info; // just avoiding warning.
}

void queueSensors(struct json_object * sensors,rd_fifoq_t *queue,int debug){
	for(int i=0;i<json_object_array_length(sensors);++i){
		// @TODO There is an increment in reference counter with this get????
		json_object *value = json_object_array_get_idx(sensors, i);
		if(debug>=2){
			Log("Push element %p\n",value);
		}
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
			main_info.debug = atoi(optarg);
			break;
		default:
			printHelp(argv[0]);
			exit(1);
		}
	}

	if(main_info.debug)
		MC_SET_DEBUG(1);

	if(configPath == NULL){
		Log("[EE] Config path not set. Exiting\n");
		exit(1);
	}

	config_file = json_object_from_file(configPath);
	if(!config_file){
		Log("[EE] Could not open config file %s\n",configPath);
		Log("Exiting\n");
		exit(1);
	}

	signal(SIGINT, sigproc);
	signal(SIGTERM, sigproc);

	assert(default_config);
	ret = parse_json_config(default_config,&worker_info,&main_info);
	assert(ret==TRUE);

	if(FALSE==json_object_object_get_ex(config_file,"conf",&config)){
		if(main_info.debug>=1)
			Log("[WW] Could not fetch \"conf\" object from config file. Using default config instead.");
	}else{
		assert(NULL!=config);
		parse_json_config(config,&worker_info,&main_info); // overwrite some or all default values.
	}

	snmp_sess_init(&worker_info.default_session); /* set defaults */
	worker_info.default_session.version  = SNMP_VERSION_1;
	pthread_mutex_init(&worker_info.session_mutex,0);

	if(FALSE==json_object_object_get_ex(config_file,"sensors",&sensors)){
		Log("[EE] Could not fetch \"sensors\" array from config file. Exiting\n");
	}else{
		rd_fifoq_init(&queue);
		init_snmp("redBorder-monitor");
		pd_thread = malloc(sizeof(pthread_t)*main_info.threads);
		if(NULL==pd_thread){
			Log("[EE] Unable to allocate threads memory. Exiting.\n");
		}else{
			worker_info.queue = &queue;
			for(int i=0;i<main_info.threads;++i){
				pthread_create(&pd_thread[i], NULL, worker, (void*)&worker_info);
			}

			while(run){
				queueSensors(sensors,&queue,main_info.debug);
				sleep(main_info.sleep_main);
			}
			Log("Leaving, wait 1sec for workers...\n");

			for(int i=0;i<main_info.threads;++i){
				pthread_join(pd_thread[i], NULL);
			}
			free(pd_thread);
		}
	}


	pthread_mutex_destroy(&worker_info.session_mutex);
	json_object_put(default_config);
	json_object_put(config_file);
	rd_fifoq_destroy(&queue);
	return ret;
}
