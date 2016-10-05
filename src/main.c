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
#include "rb_sensor_queue.h"

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
#include <json-c/json.h>
#include <json-c/printbuf.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <librd/rdthread.h>
#include <librd/rdmem.h>
#include <librdkafka/rdkafka.h>
#include <math.h>

#ifndef NDEBUG
#include <ctype.h>
#endif

static const char CONFIG_RDKAFKA_KEY[] = "rdkafka.";
static const char CONFIG_ZOOKEEPER_KEY[] = "zookeeper";
static const char CONFIG_SENSORS_KEY[] = "sensors";

static const char ENABLE_RBHTTP_CONFIGURE_OPT[] = "--enable-rbhttp";

/// Fallback config in json format
static const char *str_default_config = /* "conf:" */ "{"
    "\"debug\": 100,"
    "\"syslog\":0,"
    "\"stdout\":1,"
    "\"threads\": 10,"
    "\"timeout\": 5,"
    "\"max_snmp_fails\": 2,"
    "\"sleep_main\": 10,"
    "\"sleep_worker\": 2,"
  "}";

struct _main_info{
	const char * syslog_indent;
	uint64_t sleep_main,threads;
#ifdef HAVE_ZOOKEEPER
	struct rb_monitor_zk *zk;
#endif
};

static int run = 1;

static void sigproc(int sig) {
  static int called = 0;

  if(called++) return;
  run = 0;
  (void)sig;
}

static void printHelp(const char * progName){
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
	int64_t pop_watcher_timeout = 0,push_timeout = 0;
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
	} else if (push_timeout < 0) {
		rdlog(LOG_ERR, "Can't set a zk push timeout < 0 (%"PRId64")",
			push_timeout);
		return;
	} else if (pop_watcher_timeout < 0) {
		rdlog(LOG_ERR, "Can't set a zk pop timeout < 0 (%"PRId64")",
			pop_watcher_timeout);
	}

	main_info->zk = init_rbmon_zk(host, (uint64_t)pop_watcher_timeout,
		(uint64_t)push_timeout, zk_sensors, worker_info->queue);
}
#endif

static json_bool parse_json_config(json_object * config,struct _worker_info *worker_info,
	                                          struct _main_info *main_info){

	int ret = TRUE;
	json_object_object_foreach(config, key, val){
		errno = 0;
		if(0==strcmp(key,"debug"))
		{
			rd_log_set_severity(json_object_get_int64(val));
		}
		else if (0==strcmp(key,"stdout"))
		{
#if 0
			/// @TODO recover
			if(json_object_get_int64(val))
				worker_info->debug_output_flags |= DEBUG_STDOUT;
			else
				worker_info->debug_output_flags &= ~DEBUG_STDOUT;
#endif
		}
		else if (0==strcmp(key,"syslog"))
		{
#if 0
			/// @TODO recover
			if(json_object_get_int64(val))
				worker_info->debug_output_flags |= DEBUG_SYSLOG;
			else
				worker_info->debug_output_flags &= ~DEBUG_SYSLOG;
#endif
		}
		else if (0==strcmp(key,"threads"))
		{
			int64_t threads = json_object_get_int64(val);
			if (threads <= 0) {
				rdlog(LOG_WARNING,
					"Can't use %"PRId64" threads",
					threads);
			} else {
				main_info->threads = (uint64_t) threads;
			}
		}
		else if (0==strcmp(key,"timeout"))
		{
			worker_info->timeout = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"max_snmp_fails"))
		{
			worker_info->max_snmp_fails = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"max_kafka_fails"))
		{
			worker_info->max_kafka_fails = json_object_get_string(val);
		}
		else if (0==strcmp(key,"sleep_main"))
		{
			int64_t sleep = json_object_get_int64(val);
			if (sleep <= 0) {
				rdlog(LOG_WARNING,
					"Can't sleep for %"PRId64"\"", sleep);
			} else {
				main_info->sleep_main = (uint64_t)sleep;
			}
		}
		else if (0==strcmp(key,"kafka_broker"))
		{
			worker_info->kafka_broker = json_object_get_string(val);
		}
		else if (0==strcmp(key,"kafka_topic"))
		{
			worker_info->kafka_topic = json_object_get_string(val);
		}
		else if (0==strcmp(key,"kafka_timeout"))
		{
			worker_info->kafka_timeout = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"sleep_worker"))
		{
			worker_info->sleep_worker = json_object_get_int64(val);
		}
		else if (0 == strcmp(key, "http_endpoint"))
		{
#ifdef HAVE_RBHTTP
			worker_info->http_endpoint = json_object_get_string(val);
#else
			rdlog(LOG_ERR,
				"rb_monitor does not have librbhttp support, so"
				" %s key is invalid. Please compile it with %s",
					key, ENABLE_RBHTTP_CONFIGURE_OPT);
#endif
		}
		else if (0==strcmp(key, CONFIG_RDKAFKA_KEY))
		{
			parse_rdkafka_config_json(worker_info,key,val);
		}
		else if (0==strcmp(key,"http_max_total_connections"))
		{
			worker_info->http_max_total_connections = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"http_timeout"))
		{
			worker_info->http_timeout = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"http_connttimeout"))
		{
			worker_info->http_connttimeout = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"http_verbose"))
		{
			worker_info->http_verbose = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"http_insecure"))
		{
#ifdef HAVE_RBHTTP
			worker_info->http_insecure = json_object_get_int64(val);
#else
			rdlog(LOG_ERR,"rb_monitor does not have librbhttp support, so %s key is invalid. Please compile it with %s",
					key,ENABLE_RBHTTP_CONFIGURE_OPT);
#endif
		}
		else if (0==strcmp(key,"rb_http_max_messages"))
		{
			worker_info->rb_http_max_messages = json_object_get_int64(val);
		}
		else if (0==strcmp(key,"rb_http_mode"))
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

static void *get_report_thread(void *http_handler) {

	while (rb_http_get_reports(http_handler, msg_callback, 100) || run);

	return NULL;
}
#endif

static int worker_process_sensor_send_array(struct _worker_info * worker_info,
						rb_message_array_t *msgs) {
	for (size_t i=0; i<msgs->count; ++i) {
		char *msg = msgs->msgs[i].payload;
		size_t len = msgs->msgs[i].len;

		if(worker_info->kafka_broker != NULL) {
			rdlog(LOG_DEBUG,"[Kafka] %s\n",msg);
			const int produce_rc =  rd_kafka_produce(
				worker_info->rkt, RD_KAFKA_PARTITION_UA,
				RD_KAFKA_MSG_F_COPY,
				/* Payload and length */
				msg, len,
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
				worker_info->http_handler, msg, len,
				RB_HTTP_MESSAGE_F_COPY, err, sizeof(err), NULL);
			if (0!=produce_rc) {
				rdlog(LOG_ERR, "[HTTP] Cannot produce message: %s",
										err);
			}
		}
	#endif
		free(msg);
	}

	message_array_done(msgs);
	return 0;
}

static int worker_process_sensor_send_messages(struct _worker_info *worker_info,
					rb_message_list *msgs) {
	while(!(rb_message_list_empty(msgs))) {
		rb_message_array_t *array = rb_message_list_first(msgs);
		rb_message_list_remove(msgs, array);
		worker_process_sensor_send_array(worker_info, array);
	}

	return 0;
}

/** Process sensor
  @param worker_info Common information to all workers
  @param sensor Sensor to process
  @return OK
  */
static int worker_process_sensor(struct _worker_info *worker_info,
							rb_sensor_t *sensor) {
	rb_message_list messages;
	rb_message_list_init(&messages);

	assert(sensor);
	assert_rb_sensor(sensor);

	process_rb_sensor(worker_info, sensor, &messages);
	rb_sensor_put(sensor);

	worker_process_sensor_send_messages(worker_info, &messages);

	return 0;
}

/** Worker main function thread
  @param _info worker info
  @return provided _info
  */
static void *worker(void *_info) {
	struct _worker_info *worker_info = _info;

	rdlog(LOG_INFO,"Thread %lu connected successfuly\n.",pthread_self());
	while(run){
		rb_sensor_t *sensor = NULL;
		pthread_setcancelstate(PTHREAD_CANCEL_DISABLE,NULL);
		while((sensor = pop_sensor(worker_info->queue,100)) && run) {
			worker_process_sensor(worker_info, sensor);
		}
		pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
	}

	return _info; // just avoiding warning.
}

/** Queue sensors from array to sensors queue, increasing 1 it's reference
  counter
  @param sarray Sensors array
  @param squeue Sensors queue
  */
static void queue_sensors(rb_sensors_array_t *sarray, rd_fifoq_t *squeue) {
	for(size_t i=0; i<sarray->count; ++i) {
		rb_sensor_t *sensor = sarray->elms[i];
		rb_sensor_get(sensor);
		queue_sensor(squeue, sensor);
	}
}

static void *rdkafka_delivery_reports_poll_f(void * void_worker_info) {
	struct _worker_info *worker_info = void_worker_info;

	while(run) {
		rd_kafka_poll(worker_info->rk,500);
	}

	return NULL;
}

/** Parse sensors from config file
  @param config Sensors list
  @param sensor_json JSON config
  @return Sensors array
  */
static rb_sensors_array_t *parse_sensors(struct _worker_info *worker_info,
						struct json_object *config) {
	struct json_object *json_sensors=NULL;
	const int get_rc = json_object_object_get_ex(config, CONFIG_SENSORS_KEY,
								&json_sensors);
	if (!get_rc) {
		rdlog(LOG_ERR, "Couldn't obtain %s key. Exiting",
							CONFIG_SENSORS_KEY);
		return NULL;
	}

	if (!json_object_is_type(json_sensors, json_type_array)) {
		rdlog(LOG_ERR, "Config token %s is not an array",
			CONFIG_SENSORS_KEY);
		return NULL;
	}

	const size_t sensors_length = (size_t)json_object_array_length(
								json_sensors);
	rb_sensors_array_t *ret = rb_sensors_array_new(sensors_length);

	for (size_t i=0; i<sensors_length; ++i) {
		if (rb_sensors_array_full(ret)) {
			rdlog(LOG_CRIT,
				"Sensors array full at %zu, can't add %zu",
				ret->size, i);
			break;
		}

		json_object *json_sensor = json_object_array_get_idx(
							json_sensors, i);
		rb_sensor_t *sensor = parse_rb_sensor(json_sensor, worker_info);
		if (sensor) {
			rb_sensor_array_add(ret, sensor);
		}
	}

	return ret;
}

int main(int argc, char  *argv[])
{
	bool ret;
	char *configPath=NULL;
	char opt;
	struct json_object *config_file=NULL, *config=NULL, *zk=NULL;
	struct json_object *default_config = json_tokener_parse(
							str_default_config);
	struct _worker_info worker_info;
	struct _main_info main_info = {0};
	int debug_severity = LOG_INFO;
	pthread_t rdkafka_delivery_reports_poll_thread;

	memset(&worker_info,0,sizeof(worker_info));
	worker_info.rk_conf  = rd_kafka_conf_new();
	worker_info.rkt_conf = rd_kafka_topic_conf_new();

	pthread_t * pd_thread = NULL;
	rd_fifoq_t queue;
	sensor_queue_init(&queue);

	assert(default_config);

	worker_info.queue = &queue;

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

	rb_sensors_array_t *sensors_array = parse_sensors(&worker_info,
								config_file);
	if(sensors_array) {
		init_snmp("redBorder-monitor");
		pd_thread = malloc(sizeof(pthread_t)*main_info.threads);
		if(NULL==pd_thread){
			rdlog(LOG_CRIT,"[EE] Unable to allocate threads memory. Exiting.");
		}else{
			rdlog(LOG_INFO,"Main thread started successfuly. Starting workers threads.");
			for(size_t i=0; i<main_info.threads; ++i){
				pthread_create(&pd_thread[i], NULL, worker, (void*)&worker_info);
			}

			while(run) {
				queue_sensors(sensors_array,&queue);
				sleep(main_info.sleep_main);
			}
			rdlog(LOG_INFO,"Leaving, wait for workers...");

			for(size_t i=0; i<main_info.threads; ++i){
				pthread_join(pd_thread[i], NULL);
			}
			free(pd_thread);
		}
	}

	rb_sensors_array_done(sensors_array);

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
	sensor_queue_done(&queue);
	closelog();

	return ret;
}

