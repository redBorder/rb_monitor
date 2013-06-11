#include <stdlib.h>
#include <stdio.h>
#include <json/json.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <librd/rdqueue.h>

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
	int64_t sleep_main,threads;
};

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
}

int main(int argc, char  *argv[])
{
	char *configPath=NULL;
	int go_daemon=0,debug=0;
	char opt;
	struct json_object * config_file=NULL,*config=NULL;
	struct json_object * default_config = json_tokener_parse( str_default_config );
	struct _worker_info worker_info = {0};
	struct _main_info main_info = {0};
	assert(default_config);
	pthread_t * pd_thread;
	rd_fifoq_t * queue=NULL;


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

	if(FALSE==parse_json_config(default_config,&worker_info,&main_info));

	if(FALSE==json_object_object_get_ex(config_file,"conf",&config)){
		if(debug)
			Log("[WW] Could not fetch config file. Using default config instead.");
	}else{
		assert(NULL!=config);
		parse_json_config(config,&worker_info,&main_info); // overwrite some or all default values.
	}

	pd_thread = malloc(sizeof(pthread_t)*main_info.threads);
	if(NULL==pd_thread){
		Log("[EE] Unable to allocate threads memory. Exiting.\n");
	}else{
		for(int i=0;i<main_info.threads;++i){
			pthread_create(&pd_thread[i], NULL, worker, (void*)&worker_info);
		}

		for(int i=0;i<main_info.threads;++i){
			pthread_join(pd_thread[i], NULL);
		}
	}

	json_object_put(default_config);
	json_object_put(config_file);
	return 0;
}
