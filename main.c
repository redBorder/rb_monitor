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

#define WORKER_BUFFER_LENGHT 2048

/// Fallback config in json format
const char * str_default_config = /* "conf:" */ "{"
    "\"debug\": 3,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_fails\": 2,"
    "\"sleep_main\": 10,"
    "\"sleep_worker\": 2"
  "}";

/// Info needed by threads.
struct _worker_info{
	int64_t sleep_worker,max_fails,timeout,debug;
	rd_fifoq_t *queue;
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


void * worker(void *_info){
	struct _worker_info * worker_info = (struct _worker_info*)_info;
	while(run){
		rd_fifoq_elm_t * elm;
		while((elm = rd_fifoq_pop_timedwait(worker_info->queue,1))){
			int timeout = worker_info->timeout;
			json_object * sensor_info = elm->rfqe_ptr;
			struct printbuf* printbuf = printbuf_new();
			sprintbuf(printbuf,"{");

			if(worker_info->debug>=2)
				Log("Pop element %p\n",elm->rfqe_ptr);

			json_object_object_foreach(sensor_info, key, val){
				if(0==strncmp(key,"timeout",strlen("timeout")))
				{
					timeout = json_object_get_int64(val);
					printbuf->bpos-=1; // delete last comma
					printbuf->buf[printbuf->bpos]='\0';
				}
				else if (0==strncmp(key,"sensor_name",strlen("sensor_name")))
				{
					sprintbuf(printbuf, "\"sensor_name\":\"");
					sprintbuf(printbuf, json_object_get_string(val));
					sprintbuf(printbuf, "\"");
				}
				else if (0==strncmp(key,"sensor_id",strlen("sensor_id")))
				{
					sprintbuf(printbuf, "\"sensor_id\": ");
					sprintbuf(printbuf, json_object_get_string(val));

				}
				else if(0==strncmp(key,"monitors", strlen("monitors"))){
					for(int i=0;i<json_object_array_length(val);++i){
						json_object *value = json_object_array_get_idx(val, i);
						int kafka=0;

						/* list twice. Ugly, but faster than prepare printbuf and them discard. */
						json_object_object_foreach(value,key2,val2){
							if(0==strncmp(key2,"kafka",strlen("kafka"))){
								kafka=1;
								break;
							}
						}

						if(kafka){
							json_object_object_foreach(value,key2,val2){

								if(0==strncmp(key2,"name",strlen("name")) || 0==strncmp(key2,"unit",strlen("unit"))){
									sprintbuf(printbuf,"\"%s\":\"%s\"",key2,json_object_get_string(val2));
								}else if(0==strncmp(key2,"kafka",strlen("kafka"))){
									printbuf->bpos-=1; // delete last comma.
									                   // next printbuf(',') will put \0.
								}else {
									if(worker_info->debug>=1)
										Log("Cannot parse %s argument\n",key2);
								}
								sprintbuf(printbuf,",");
							}
						}
					}
				}
				sprintbuf(printbuf, ",");
			}
			printbuf->bpos-=2; // delete last comma (and pass \0)
			sprintbuf(printbuf,"}");
			//char * str_to_kafka = printbuf->buf;
			//printbuf->buf=NULL;
			if(worker_info->debug>=3)
				Log("[Kafka] %s\n",printbuf->buf);
			printbuf_free(printbuf);
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
