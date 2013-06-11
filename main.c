#include <stdlib.h>
#include <stdio.h>
#include <json/json.h>
#include <unistd.h>
#include <stdarg.h>
#include <errno.h>
#include <assert.h>


const char * str_default_config = "\"conf\": {"
    "\"debug\": 3,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_fails\": 2,"
    "\"sleep_main_thread\": 10,"
    "\"sleep_worker_thread\": 2"
  "}";


Log(char *fmt,...){
	va_list ap;
	va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}


printHelp(const char * progName){
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

int main(int argc, char  *argv[])
{
	char *configPath=NULL;
	int go_daemon=0,debug=0;
	char opt;
	struct json_object * config=NULL;
	struct json_object * default_config = json_tokener_parse( str_default_config );
	assert(default_config);


	while ((opt = getopt(argc, argv, "gc:hvd")) != -1) {
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
			debug = 1;
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

	config = json_object_from_file(configPath);
	if(!config){
		Log("[EE] Could not open config file %s\n",configPath);
		Log("Exiting\n");
		exit(1);
	}



	json_object_put(default_config);
	json_object_put(config);
	return 0;
}
