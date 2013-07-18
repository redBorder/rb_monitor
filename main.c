#include <stdlib.h>
#include <stdio.h>
#include <json/json.h>
#include <json/printbuf.h>
#include <unistd.h>
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

/// Fallback config in json format
const char * str_default_config = /* "conf:" */ "{"
    "\"debug\": 3,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_fails\": 2,"
    "\"sleep_main\": 10,"
    "\"sleep_worker\": 2,"
    "\"kafka_broker\": \"localhost\","
    "\"kafka_topic\": \"SNMP\","
    "\"kafka_start_partition\": 0,"
    "\"kafka_end_partition\": 2"
  "}";

/// Info needed by threads.
struct _worker_info{
	const char * community,*kafka_broker,*kafka_topic;
	int64_t sleep_worker,max_fails,timeout,kafka_start_partition,kafka_current_partition,kafka_end_partition,debug;
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
	struct snmp_session session;
	rd_kafka_t * rk;
	int thread_ok;
	struct _sensor_data sensor_data;
};

struct _main_info{
	int64_t sleep_main,threads,debug;
};

int run = 1;
void sigproc(int sig) {
  static int called = 0;

  if(called) return; else called = 1;
  run = 0;
  (void)sig;
}

void Log(char *fmt,...){
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
	

	json_object_object_foreach(config, key, val){
		if(0==strncmp(key,"debug",sizeof "debug"-1)){
			errno = 0;
			worker_info->debug = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","debug\n",strerror(errno));
		}
		else if(0==strncmp(key,"threads",sizeof "threads"-1))
		{
			errno = 0;
			main_info->threads = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","threads\n",strerror(errno));
		}
		else if(0==strncmp(key,"timeout",sizeof "timeout"-1))
		{
			errno = 0;
			worker_info->timeout = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","timeout\n",strerror(errno));
		}
		else if(0==strncmp(key,"max_fails",sizeof "max_fails"-1))
		{
			errno = 0;
			worker_info->max_fails = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","max_fails\n",strerror(errno));
		}
		else if(0==strncmp(key,"sleep_main",sizeof "sleep_main"-1))
		{
			errno = 0;
			main_info->sleep_main = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","sleep_main\n",strerror(errno));
		}
		else if(0==strncmp(key,"kafka_broker", strlen("kafka_broker")))
		{
			worker_info->kafka_broker	= json_object_get_string(val);
		}
		else if(0==strncmp(key,"kafka_topic", strlen("kafka_topic")))
		{
			worker_info->kafka_topic	= json_object_get_string(val);
		}
		else if(0==strncmp(key,"kafka_start_partition", strlen("kafka_start_partition")))
		{
			worker_info->kafka_start_partition = worker_info->kafka_end_partition = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"kafka_end_partition", strlen("kafka_end_partition")))
		{
			worker_info->kafka_end_partition = json_object_get_int64(val);
		}
		else if(0==strncmp(key,"sleep_worker",sizeof "sleep_worker"-1))
		{
			errno = 0;
			worker_info->sleep_worker = json_object_get_int64(val);
			if(errno!=0)
				Log("[EE] Could not parse %s value: %s","sleep_worker_thread\n",strerror(errno));
		}else{
			Log("[EE] Don't know what config.%s key means.\n",key);
		}
	}
	return TRUE;
}

struct libmatheval_stuffs{
	const char ** names;
	double *values;
	unsigned int variables_pos;
	unsigned int total_lenght;
};

int libmatheval_append(struct libmatheval_stuffs *matheval,const char *name,double val){
	if(matheval->variables_pos<matheval->total_lenght){
		matheval->names[matheval->variables_pos] = name;
		matheval->values[matheval->variables_pos++] = val;
	}else{
		Log("[FIX] More variables than I can save in line %d. Have to fix\n",__LINE__);
		return 0;
	}
	return 1;
}

/* @warning This function assumes ALL fields of sensor_data will be populated */
int process_sensor_monitors(struct _worker_info *worker_info,struct _perthread_worker_info *pt_worker_info,
			json_object * monitors)
{
	int aok=1;
	struct _sensor_data *sensor_data = &pt_worker_info->sensor_data;

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
		if(!sensor_data->peername){
			Log("[EE] Community not setted in %s. Skipping.\n",sensor_data->sensor_name);
			aok=0;
		}
	}

	pt_worker_info->session.peername = (char *)sensor_data->peername; /* We trust in session. It will not modify it*/
	pt_worker_info->session.community = (unsigned char *)sensor_data->community; /* We trust in session. It will not modify it*/
	pt_worker_info->session.community_len = strlen(sensor_data->community);
	struct snmp_session *ss;
	if(NULL == (ss = snmp_open(&pt_worker_info->session))){
		Log("Error creating session: %s",snmp_errstring(pt_worker_info->session.s_snmp_errno));
		aok=0;
	}

	for(int i=0;ss && aok && i<json_object_array_length(monitors);++i){
		struct timeval tv;
		gettimeofday(&tv,NULL);

		const char * name = NULL;
		json_object *value = json_object_array_get_idx(monitors, i);
		int kafka=0;
		struct printbuf* printbuf;

		/* list twice. Ugly, but faster than prepare printbuf and them discard. */
		json_object_object_foreach(value,key,val){
			if(0==strncmp(key,"kafka",strlen("kafka")) || 0==strncmp(key,"op",strlen("op"))){
				kafka=1;
				break;
			}
		}

		if(kafka){
			printbuf = printbuf_new();
			sprintbuf(printbuf,"{");
			sprintbuf(printbuf,"\"event_timestamp\":%lu,",tv.tv_sec*1000 + tv.tv_usec/1000);
			sprintbuf(printbuf,"\"sensor_id\":%lu,",sensor_data->sensor_id);
		}

		json_object_object_foreach(value,key2,val2){
			if(0==strncmp(key2,"name",strlen("name"))){ 
				name = json_object_get_string(val2);
			}else if(0==strncmp(key2,"unit",strlen("unit"))){
				if(printbuf)
					sprintbuf(printbuf, "\"%s\":\"%s\",",key2,json_object_get_string(val2));
			}else if(0==strncmp(key2,"kafka",strlen("kafka"))){
				// Do nothing
			}else if(0==strncmp(key2,"oid",strlen("oid"))){
				assert(sensor_data->sensor_name);
				/* @TODO test passing a sensor without params to caller function. */
				if(!name){
					if(worker_info->debug>=1)
						Log("[WW] name of param not set in %s. Skipping\n",sensor_data->sensor_name);
					continue /*foreach*/;
				}
				if(printbuf){
					sprintbuf(printbuf, "\"sensor_name\":\"%s\",",sensor_data->sensor_name);
					sprintbuf(printbuf, "\"monitor\":\"%s\",",name);
					sprintbuf(printbuf, "\"type\":\"monitor\",");
				}
				
				struct snmp_pdu *pdu=snmp_pdu_create(SNMP_MSG_GET);
				struct snmp_pdu *response=NULL;
				oid entry_oid[MAX_OID_LEN];
				size_t entry_oid_len = MAX_OID_LEN;
				read_objid(json_object_get_string(val2),entry_oid,&entry_oid_len);
				snmp_add_null_var(pdu,entry_oid,entry_oid_len);
				int status = snmp_synch_response(ss,pdu,&response);
				if(status==STAT_SUCCESS && response->errstat == SNMP_ERR_NOERROR){
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
							if(printbuf)
								sprintbuf(printbuf,"\"value\":%d,",*response->variables->val.integer);
							if(worker_info->debug>=3){
								Log("Saving %s var in libmatheval array. OID=%s;Value=%d\n",
									name,json_object_get_string(val2),*response->variables->val.integer);
							}
							libmatheval_append(&matheval,name,*response->variables->val.integer);
						
							break;
						case ASN_OCTET_STR:
							/* We don't know if it's a double inside a string; We try to convert and save */
							number = atol((const char *)response->variables->val.string);
							if(worker_info->debug>=3){
								Log("Saving %lf var in libmatheval array. OID=%s;Value=\"%lf\"\n",name,json_object_get_string(val2),number);
							}
							libmatheval_append(&matheval,name,number);

							if(printbuf){
								char * aux = malloc(sizeof(char)*(response->variables->val_len + 1));
								snprintf(aux, response->variables->val_len + 1, "%s", 
									response->variables->val.string);
								sprintbuf(printbuf,"\"value\":\"%s\",",aux);
								free(aux);
							}
							break;
						default:
							Log("[WW] Unknow variable type %d in line %d\n",response->variables->type,__LINE__);
					};
					#ifndef NDEBUG
					int i=0;
					for(i=0;printbuf && i<printbuf->bpos;++i)
						assert(isprint(printbuf->buf[i] ));
					#endif
					snmp_free_pdu(response);

				}else if(worker_info->debug){
					if (status == STAT_SUCCESS)
						Log("Error in packet\nReason: %s\n",snmp_errstring(response->errstat));
					else
						Log("Snmp error: %s\n", snmp_api_errstring(ss->s_snmp_errno));
				}
			}else if(0==strncmp(key2,"op",strlen("op"))){
				/* @TODO buffer this! */
				void *f = evaluator_create ((char *)json_object_get_string(val2)); /*really it has to do (void *). See libmatheval doc. */
				double d = evaluator_evaluate (f, matheval.variables_pos ,(char **) matheval.names,matheval.values);
				evaluator_destroy (f);
				if(worker_info->debug>=4)
					Log("Result of operation %s: %lf\n",key2,d);
				/* op will send by default, so we ignore kafka param */
				libmatheval_append(&matheval, name,d);

				if(printbuf){
					sprintbuf(printbuf, "\"sensor_name\":\"%s\",",sensor_data->sensor_name);
					sprintbuf(printbuf, "\"monitor\":\"%s\",",name);
					sprintbuf(printbuf, "\"type\":\"monitor\",");
					sprintbuf(printbuf, "\"value\":\"%lf\",",d);
				}

			}else{
				if(worker_info->debug>=1)
					Log("Cannot parse %s argument (line %d)\n",key2,__LINE__);
			}
		}

		if(printbuf){
			printbuf->bpos--; /* delete last comma */
			sprintbuf(printbuf,"}");

			//char * str_to_kafka = printbuf->buf;
			//printbuf->buf=NULL;
			if(sensor_data->peername && sensor_data->sensor_name && sensor_data->community){
				if(worker_info->debug>=3)
					Log("[Kafka:%d] %s\n",worker_info->kafka_current_partition,printbuf->buf);
				if(0==rd_kafka_produce(pt_worker_info->rk, (char *)worker_info->kafka_topic, 
						worker_info->kafka_current_partition++, 
						RD_KAFKA_OP_F_FREE, printbuf->buf, printbuf->bpos))
					printbuf->buf=NULL; // rdkafka will free it
			}
			printbuf_free(printbuf);
			if(worker_info->kafka_current_partition>worker_info->kafka_end_partition)
				worker_info->kafka_current_partition = worker_info->kafka_start_partition;
			printbuf = NULL;
		}

	}
	snmp_close(ss);

	return aok;
} 

int process_sensor(struct _worker_info * worker_info,struct _perthread_worker_info *pt_worker_info,json_object *sensor_info){
	memset(&pt_worker_info->sensor_data,1,sizeof(pt_worker_info->sensor_data));
	pt_worker_info->sensor_data.timeout = worker_info->timeout;
	json_object * monitors = NULL;
	int aok = 1;

	json_object_object_foreach(sensor_info, key, val){
		if(0==strncmp(key,"timeout",strlen("timeout"))){
			pt_worker_info->sensor_data.timeout = json_object_get_int64(val);
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


	if(aok){
		if(pt_worker_info->sensor_data.sensor_name == NULL){
			aok = 0;
			if(worker_info->debug>=1){
				Log("[CONFIG] Sensor_name not setted in some sensor\n");
			}
		}
	}if(aok){
		if(pt_worker_info->sensor_data.peername == NULL){
			aok = 0;
			if(worker_info->debug>=1){
				Log("[CONFIG] Peername not setted in sensor %s",
					pt_worker_info->sensor_data.sensor_name?pt_worker_info->sensor_data.sensor_name:"(some sensor)");
			}
		}
	}if(aok){
		if(pt_worker_info->sensor_data.community == NULL){
			aok = 0;
			if(worker_info->debug>=1){
				Log("[CONFIG] Community not setted in sensor %s",
					pt_worker_info->sensor_data.sensor_name?pt_worker_info->sensor_data.sensor_name:"(some sensor)");
			}
		}
	}if(aok){
		if(NULL==monitors){
			aok = 0;
			if(worker_info->debug>=1){
				Log("[CONFIG] Monitors not setted in sensor %s",
					pt_worker_info->sensor_data.sensor_name?pt_worker_info->sensor_data.sensor_name:"(some sensor)");
			}
		}
	}
	
	if(aok)
		aok = process_sensor_monitors(worker_info, pt_worker_info, monitors);

	return aok;
}


void * worker(void *_info){
	struct _worker_info * worker_info = (struct _worker_info*)_info;
	struct _perthread_worker_info pt_worker_info;
	
	snmp_sess_init(&pt_worker_info.session); /* set defaults */
	pt_worker_info.session.version  = SNMP_VERSION_1;
	
	pt_worker_info.thread_ok = 1;

	if (!(pt_worker_info.rk = rd_kafka_new(RD_KAFKA_PRODUCER, worker_info->kafka_broker, NULL))) {
		if(worker_info->debug>=1)
			Log("Error calling kafka_new producer: %s\n",strerror(errno));
		pt_worker_info.thread_ok=0;
	}
	
	while(pt_worker_info.thread_ok && run){
		rd_fifoq_elm_t * elm;
		while((elm = rd_fifoq_pop_timedwait(worker_info->queue,1))){
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
	int go_daemon=0,debug=0; // @todo delete duplicity of debug
	char opt;
	struct json_object * config_file=NULL,*config=NULL,*sensors=NULL;
	struct json_object * default_config = json_tokener_parse( str_default_config );
	struct _worker_info worker_info = {0};
	struct _main_info main_info = {0};
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
			go_daemon = 1;
			break;
		case 'c':
			configPath = optarg;
			break;
		case 'd':
			debug = atoi(optarg);
			break;
		default:
			printHelp(argv[0]);
			exit(1);
		}
	}

	if(debug)
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


	ret = parse_json_config(default_config,&worker_info,&main_info);
	assert(ret==TRUE);

	if(FALSE==json_object_object_get_ex(config_file,"conf",&config)){
		if(debug>=1)
			Log("[WW] Could not fetch \"conf\" object from config file. Using default config instead.");
	}else{
		assert(NULL!=config);
		parse_json_config(config,&worker_info,&main_info); // overwrite some or all default values.
	}

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
				queueSensors(sensors,&queue,debug);
				sleep(main_info.sleep_main);
			}
			Log("Leaving, wait 1sec for workers...\n");

			for(int i=0;i<main_info.threads;++i){
				pthread_join(pd_thread[i], NULL);
			}
		}
	}

	json_object_put(default_config);
	json_object_put(config_file);
	rd_fifoq_destroy(&queue);
	return 0;
}
