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

#include "rb_monitor_zk.h"

#ifdef HAVE_ZOOKEEPER

#include "rb_sensor.h"
#include "rb_sensor_queue.h"
#include "rb_zk.h"

#include <librd/rdlog.h>
#include <librd/rdmem.h>
#include <librd/rdtimer.h>

#include <assert.h>
#include <errno.h>
#include <string.h>

/* Zookeeper path to save data */
#define ZOOKEEPER_TASKS_PATH "/rb_monitor/sensors"
#define ZOOKEEPER_TASKS_PATH_LEAF "/rb_monitor/sensors/sensor_"
#define ZOOKEEPER_MUTEX_PATH "/rb_monitor/lock"
#define ZOOKEEPER_MUTEX_PATH_LEAF ZOOKEEPER_MUTEX_PATH "/sensors_list_"
#define ZOOKEEPER_LEADER_PATH "/rb_monitor/leader"
#define ZOOKEEPER_LEADER_LEAF_NAME ZOOKEEPER_LEADER_PATH "/leader_prop_"

#define RB_MONITOR_ZK_MAGIC 0xB010A1C0B010A1C0L

static const int zk_read_timeout = 10000;

struct string_list_node {
	char *str;
	int len;
#define STRING_LIST_STR_F_COPY 0x01
#define STRING_LIST_STR_F_FREE 0x02
	int flags;
	TAILQ_ENTRY(string_list_node) entry;
};

static void string_list_node_done(struct string_list_node *node) {
	if (node->flags & STRING_LIST_STR_F_FREE) {
		free(node->str);
	}
	free(node);
}

typedef struct {
	TAILQ_HEAD(, string_list_node) list;
#define STRING_LIST_F_LOCK 0x01
	int flags;
	pthread_mutex_t mutex;
} string_list;

#define string_list_have_to_lock(list) (list->flags & STRING_LIST_F_LOCK)

static void string_list_init(string_list *list, int flags) {
	TAILQ_INIT(&list->list);

	list->flags = flags;
	if (string_list_have_to_lock(list)) {
		pthread_mutex_init(&list->mutex, NULL);
	}
}

static struct string_list_node *
string_list_append(string_list *l, char *str, size_t len, int flags) {
	struct string_list_node *node = NULL;
	const size_t alloc_size =
			sizeof(*node) +
			((flags & STRING_LIST_STR_F_COPY) ? len + 1 : 0);
	node = calloc(1, alloc_size);

	if (node) {
		node->str = flags & STRING_LIST_STR_F_COPY ? (void *)&node[1]
							   : str;
		node->len = len;
		node->flags = flags;
		if (flags & STRING_LIST_STR_F_COPY) {
			memcpy(node->str, str, len);
			node->str[len] = '\0';
		}
		TAILQ_INSERT_TAIL(&l->list, node, entry);
	}

	return node;
}

static struct string_list_node *string_list_append_const(string_list *l,
							 const char *str,
							 size_t len,
							 int flags) {
	// couldn't free a const field
	assert(flags & ~STRING_LIST_STR_F_FREE);

	// Const casting
	char *_str;
	memcpy(&_str, &str, sizeof(str));

	return string_list_append(l, _str, len, flags);
}

static void
string_list_foreach_arg0(string_list *list,
			 void (*function)(char *str, size_t len, void *arg),
			 void *arg,
			 int _free) {
	struct string_list_node *var, *aux;

	if (string_list_have_to_lock(list)) {
		pthread_mutex_lock(&list->mutex);
	}

	TAILQ_FOREACH_SAFE(var, aux, &list->list, entry) {
		function(var->str, var->len, arg);
		if (_free) {
			string_list_node_done(var);
		}
	}

	if (string_list_have_to_lock(list)) {
		pthread_mutex_unlock(&list->mutex);
	}
}

static void
string_list_foreach_arg_free(string_list *list,
			     void (*function)(char *str, size_t len, void *arg),
			     void *arg) {
	string_list_foreach_arg0(list, function, arg, 1);
}

static void
string_list_foreach_arg(string_list *list,
			void (*function)(char *str, size_t len, void *arg),
			void *arg) {
	string_list_foreach_arg0(list, function, arg, 0);
}

/// Moves all elements of l2 into the end of l1
static void string_list_move(string_list *l1, string_list *l2) {
	if (string_list_have_to_lock(l1)) {
		pthread_mutex_lock(&l1->mutex);
	}
	if (string_list_have_to_lock(l2)) {
		pthread_mutex_lock(&l2->mutex);
	}

	TAILQ_CONCAT(&l1->list, &l2->list, entry);
	TAILQ_INIT(&l2->list);

	if (string_list_have_to_lock(l2)) {
		pthread_mutex_unlock(&l2->mutex);
	}
	if (string_list_have_to_lock(l1)) {
		pthread_mutex_unlock(&l1->mutex);
	}
}

struct rb_monitor_zk {
#ifdef RB_MONITOR_ZK_MAGIC
	uint64_t magic;
#endif
	rd_timer_t timer;
	rd_thread_t *worker;

	char *zk_host;
	time_t pop_watcher_timeout, push_timeout;

	string_list pop_sensors_list;
	string_list push_sensors_list;

	/// @TODO reset function that set to 0 this fields
	char *my_leader_node;
	int i_am_leader;

	rd_fifoq_t *workers_queue;

	struct rb_zk *zk_handler;
};

static struct rb_monitor_zk *rb_monitor_zk_casting(void *a) {
	struct rb_monitor_zk *b = a;
#ifdef RB_MONITOR_ZK_MAGIC
	assert(RB_MONITOR_ZK_MAGIC == b->magic);
#endif
	return b;
}

static struct rb_monitor_zk *rb_monitor_zk_const_casting(const void *a) {
	struct rb_monitor_zk *b = NULL;
	memcpy(&b, &a, sizeof(a));
	return rb_monitor_zk_casting(b);
}

/*
 *  LIST POP
 */

static void sensors_queue_poll_loop_start(struct rb_monitor_zk *rb_mzk);

void rb_zk_pop_error_cb(struct rb_zk *rb_zk,
			struct rb_zk_queue_element *qelm,
			const char *cause,
			int rc,
			void *opaque) {

	rdlog(LOG_ERR,
	      "Couldn't pop queue element [rc=%d]: %s. Retrying",
	      rc,
	      cause);
	sensors_queue_poll_loop_start(
			(struct rb_monitor_zk *)rb_zk_queue_element_opaque(
					qelm));
}

static void
rb_monitor_zk_add_sensor_to_monitor_queue(char *str, size_t len, void *opaque) {
	struct rb_monitor_zk *rb_mzk = rb_monitor_zk_casting(opaque);
	enum json_tokener_error jerr;
	assert(rb_mzk->workers_queue);

	json_object *obj = json_tokener_parse_verbose(str, &jerr);
	if (NULL == obj) {
		rdlog(LOG_ERR,
		      "Can't parse zookeeper received JSON: %s",
		      json_tokener_error_desc(jerr));
		return;
	}

	queue_sensor(rb_mzk->workers_queue, obj);
}

static void rb_monitor_zk_add_popped_sensors_to_monitor_queue(
		struct rb_monitor_zk *rb_mzk) {
	string_list laux;
	string_list_init(&laux, 0);

	string_list_move(&laux, &rb_mzk->pop_sensors_list);
	string_list_foreach_arg_free(&laux,
				     rb_monitor_zk_add_sensor_to_monitor_queue,
				     rb_mzk);
}

void rb_zk_pop_data_cb(int rc,
		       const char *value,
		       int value_len,
		       const struct Stat *stat,
		       const void *data) {
	struct rb_monitor_zk *rb_mzk = rb_monitor_zk_const_casting(data);

	if (rc < 0) {
		rdlog(LOG_ERR, "Error getting data [rc=%d]", rc);
	}

	rdlog(LOG_DEBUG, "Received data %*.s", value_len, value);
	string_list_append_const(&rb_mzk->pop_sensors_list,
				 value,
				 value_len,
				 STRING_LIST_STR_F_COPY);
	rd_thread_func_call1(rb_mzk->worker,
			     rb_monitor_zk_add_popped_sensors_to_monitor_queue,
			     rb_mzk);
}

void rb_zk_pop_delete_completed_cb(int rc, const void *data) {
	struct rb_monitor_zk *rbmzk = rb_monitor_zk_const_casting(data);

	if (rc < 0) {
		rdlog(LOG_ERR, "Error deleting [rc=%d]", rc);
	} else {
		rdlog(LOG_DEBUG, "Deleting sensor in zookeeper, pop completed");
	}

	sensors_queue_poll_loop_start(rbmzk);
}

static void sensors_queue_poll_loop_start(struct rb_monitor_zk *rb_mzk) {
	struct rb_zk_queue_element *qelement_config =
			new_queue_element(rb_mzk->zk_handler,
					  ZOOKEEPER_TASKS_PATH_LEAF,
					  rb_zk_pop_error_cb,
					  rb_zk_pop_data_cb,
					  rb_zk_pop_delete_completed_cb,
					  rb_mzk);

	rb_zk_queue_pop(rb_mzk->zk_handler,
			qelement_config,
			ZOOKEEPER_MUTEX_PATH_LEAF);
}

/*
 *  LIST PUSH
 */

static void
rb_monitor_leader_push_sensors_cb(int rc, const char *value, const void *data) {
	if (rc < 0) {
		rdlog(LOG_ERR,
		      "Error pushing element %s in the sensors queue. "
		      "rc=%d",
		      value,
		      rc);
	}
}

static void rb_monitor_push_sensor(char *str, size_t len, void *_arg) {
	struct rb_monitor_zk *rb_mzk = _arg;
	rdlog(LOG_DEBUG, "uploading sensor [%s]", str);
	rb_zk_queue_push(rb_mzk->zk_handler,
			 ZOOKEEPER_TASKS_PATH_LEAF,
			 str,
			 len,
			 rb_monitor_leader_push_sensors_cb,
			 rb_mzk);
}

static void rb_monitor_leader_push_sensors(void *opaque) {
	struct rb_monitor_zk *rb_mzk = rb_monitor_zk_casting(opaque);
	string_list_foreach_arg(&rb_mzk->push_sensors_list,
				rb_monitor_push_sensor,
				rb_mzk);
}

/*
 *  RB_MONITOR ZOOKEEPER MASTER
 */

static void try_to_be_master(struct rb_monitor_zk *rb_mzk);

static void
leader_lock_status_chage_cb(struct rb_zk_mutex *mutex, void *opaque) {
	struct rb_monitor_zk *monitor_zk = rb_monitor_zk_casting(opaque);

	monitor_zk->i_am_leader = rb_zk_mutex_obtained(mutex);

	rdlog(LOG_DEBUG,
	      "Mutex %s status change: %d",
	      rb_zk_mutex_path(mutex),
	      rb_zk_mutex_obtained(mutex));

	if (monitor_zk->i_am_leader) {
		rd_timer_start(&monitor_zk->timer,
			       monitor_zk->push_timeout * 1000);
	} else {
		rd_timer_stop(&monitor_zk->timer);
	}
}

static void leader_lock_error_cb(struct rb_zk *rb_zk,
				 struct rb_zk_mutex *mutex,
				 const char *cause,
				 int rc,
				 void *opaque) {
	struct rb_monitor_zk *monitor_zk = rb_monitor_zk_casting(opaque);

	rdlog(LOG_ERR, "Can't get ZK leader status: [%s][rc=%d]", cause, rc);
	try_to_be_master(monitor_zk);
}

static void try_to_be_master(struct rb_monitor_zk *rb_mzk) {
	rb_zk_mutex_lock(rb_mzk->zk_handler,
			 ZOOKEEPER_LEADER_LEAF_NAME,
			 leader_lock_status_chage_cb,
			 leader_lock_error_cb,
			 rb_mzk);
}

/* Prepare zookeeper structure */
static int zk_prepare(struct rb_zk *zh) {
	rdlog(LOG_DEBUG, "Preparing zookeeper structure");
	return rb_zk_create_recursive_node(zh, ZOOKEEPER_TASKS_PATH, 0) &&
	       rb_zk_create_recursive_node(zh, ZOOKEEPER_MUTEX_PATH, 0) &&
	       rb_zk_create_recursive_node(zh, ZOOKEEPER_LEADER_PATH, 0);
}

/// @TODO use client id too.
static void *zk_mon_watcher(void *_context) {
	rd_thread_dispatch();
	return NULL;
}

/*
 * RB_MONITOR ZOOKEEPER STRUCT (pt 2)
 */

static size_t rb_monitor_zk_parse_sensors(struct rb_monitor_zk *monitor_zk,
					  json_object *zk_sensors) {
	int i = 0;
	for (i = 0; i < json_object_array_length(zk_sensors); ++i) {
		json_object *value = json_object_array_get_idx(zk_sensors, i);
		if (NULL == value) {
			rdlog(LOG_ERR, "ZK sensor %d couldn't be getted", i);
			continue;
		}

		const char *sensor_str = json_object_to_json_string(value);
		if (NULL == sensor_str) {
			rdlog(LOG_ERR,
			      "Can't convert zk sensor %d to string",
			      i);
			continue;
		}

		void *rc = string_list_append_const(
				&monitor_zk->push_sensors_list,
				sensor_str,
				strlen(sensor_str),
				STRING_LIST_STR_F_COPY);
		if (rc == NULL) {
			rdlog(LOG_ERR,
			      "Can't append ZK sensor %d to string "
			      "list (out of memory?)",
			      i);
			continue;
		}
	}

	return i;
}

struct rb_monitor_zk *init_rbmon_zk(char *host,
				    uint64_t pop_watcher_timeout,
				    uint64_t push_timeout,
				    json_object *zk_sensors,
				    rd_fifoq_t *workers_queue) {
	char strerror_buf[BUFSIZ];

	assert(host);
	assert(zk_sensors);
	assert(workers_queue);

	struct rb_monitor_zk *_zk = calloc(1, sizeof(*_zk));
	if (NULL == _zk) {
		rdlog(LOG_ERR,
		      "Can't allocate zookeeper handler (out of "
		      "memory?)");
		return NULL;
	}

#ifdef RB_MONITOR_ZK_MAGIC
	_zk->magic = RB_MONITOR_ZK_MAGIC;
#endif

	_zk->zk_host = host;
	_zk->pop_watcher_timeout = pop_watcher_timeout;
	_zk->push_timeout = push_timeout;
	_zk->zk_handler = rb_zk_init(_zk->zk_host, pop_watcher_timeout);
	rd_thread_create(&_zk->worker, NULL, NULL, zk_mon_watcher, _zk);
	rd_timer_init(&_zk->timer,
		      RD_TIMER_RECURR,
		      _zk->worker,
		      rb_monitor_leader_push_sensors,
		      _zk);

	_zk->workers_queue = workers_queue;
	string_list_init(&_zk->pop_sensors_list, STRING_LIST_F_LOCK);
	string_list_init(&_zk->push_sensors_list, 0);
	rb_monitor_zk_parse_sensors(_zk, zk_sensors);

	if (NULL == _zk->zk_handler) {
		strerror_r(errno, strerror_buf, sizeof(strerror_buf));
		rdlog(LOG_ERR, "Can't init zookeeper: [%s].", strerror_buf);
		goto err;
	} else {
		rdlog(LOG_ERR, "Connected to ZooKeeper %s", _zk->zk_host);
	}

	zk_prepare(_zk->zk_handler);
	try_to_be_master(_zk);
	sensors_queue_poll_loop_start(_zk);

	return _zk;
err:
	/// @TODO error treatment
	return NULL;
}

#endif /* HAVE_ZOOKEEPER */
