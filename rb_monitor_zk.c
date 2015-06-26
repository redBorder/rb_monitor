/*
** Copyright (C) 2014 Eneo Tecnologia S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

#include "rb_monitor_zk.h"

#include "rb_zk.h"

#include <librd/rdlog.h>
#include <librd/rdtimer.h>
#include <librd/rdmem.h>
#include <assert.h>
#include <string.h>
#include <errno.h>

/* Zookeeper path to save data */
static const char ZOOKEEPER_TASKS_PATH[]  = "/rb_monitor/tasks";
static const char ZOOKEEPER_LOCK_PATH[]   = "/rb_monitor/lock";
#define ZOOKEEPER_LEADER_PATH "/rb_monitor/leader"
#define ZOOKEEPER_LEADER_LEAF_NAME ZOOKEEPER_LEADER_PATH "/leader_prop_"

#define RB_MONITOR_ZK_MAGIC 0xB010A1C0B010A1C0L

static const int zk_read_timeout = 10000;

struct string_list_node {
  char *str;
  TAILQ_ENTRY(string_list_node) entry;
};

typedef TAILQ_HEAD(,string_list_node) string_list;

#define string_list_init(head) TAILQ_INIT(head)

static struct string_list_node *string_list_append(string_list *l,const char *str) {
  struct string_list_node *node = NULL;
  rd_calloc_struct(&node, sizeof(*node), 
    -1, str, &node->str,
    RD_MEM_END_TOKEN);
  
  if(node) {
    TAILQ_INSERT_TAIL(l,node,entry);
  }

  return node;
}

#if 0
static void string_list_done(string_list *l) {
  struct string_list_node *i,*aux;
  TAILQ_FOREACH_SAFE(i,aux,l,entry) {
    free(i);
  }
}
#endif

struct rb_monitor_zk {
#ifdef RB_MONITOR_ZK_MAGIC
	uint64_t magic;
#endif

  char *zk_host;
  time_t pop_watcher_timeout,push_timeout;

  string_list sensors_list;

  /// @TODO reset function that set to 0 this fields
  char *my_leader_node;
  int i_am_leader;

  struct rb_zk *zk_handler;
};

static size_t rb_monitor_zk_parse_sensors(struct rb_monitor_zk *monitor_zk,
                                                  json_object *zk_sensors) {
  string_list_init(&monitor_zk->sensors_list);
  
  int i=0;
  for(i=0;i<json_object_array_length(zk_sensors);++i) {
    json_object *value = json_object_array_get_idx(zk_sensors, i);
    if(NULL == value) {
      rdlog(LOG_ERR,"ZK sensor %d couldn't be getted",i);
      continue;
    }

    const char *sensor_str = json_object_to_json_string(value);
    if(NULL == sensor_str) {
      rdlog(LOG_ERR,"Can't convert zk sensor %d to string",i);
      continue;
    }

    void *rc = string_list_append(&monitor_zk->sensors_list,sensor_str);
    if(rc == NULL) {
      rdlog(LOG_ERR,"Can't append ZK sensor %d to string list (out of memory?)",i);
      continue;
    }
  }

  return i;
}

static struct rb_monitor_zk *rb_monitor_zk_casting(void *a) {
	struct rb_monitor_zk *b=a;
#ifdef RB_MONITOR_ZK_MAGIC
	assert(RB_MONITOR_ZK_MAGIC == b->magic);
#endif
	return b;
}

static void try_to_be_master(struct rb_monitor_zk *rb_mzk);

static void leader_lock_status_chage_cb(struct rb_zk_mutex *mutex,void *opaque) {
	struct rb_monitor_zk *monitor_zk = rb_monitor_zk_casting(opaque);

	monitor_zk->i_am_leader = rb_zk_mutex_obtained(mutex);

	rdlog(LOG_DEBUG,"Mutex %s status change: %d",rb_zk_mutex_path(mutex),
		rb_zk_mutex_obtained(mutex));
}

static void leader_lock_error_cb(struct rb_zk *rb_zk, struct rb_zk_mutex *mutex,
	const char *cause,int rc,void *opaque) {
  struct rb_monitor_zk *monitor_zk = rb_monitor_zk_casting(opaque);

	rdlog(LOG_ERR,"Can't get ZK leader status: [%s][rc=%d]",cause,rc);
	try_to_be_master(monitor_zk);
}

static void try_to_be_master(struct rb_monitor_zk *rb_mzk) {
  rb_zk_mutex_lock(rb_mzk->zk_handler,ZOOKEEPER_LEADER_LEAF_NAME,
  leader_lock_status_chage_cb,leader_lock_error_cb,rb_mzk);
}

/* Prepare zookeeper structure */
static int zk_prepare(struct rb_zk *zh) {
  rdlog(LOG_DEBUG,"Preparing zookeeper structure");
  return rb_zk_create_recursive_node(zh,ZOOKEEPER_TASKS_PATH,0) &&
    rb_zk_create_recursive_node(zh,ZOOKEEPER_LOCK_PATH,0) &&
    rb_zk_create_recursive_node(zh,ZOOKEEPER_LEADER_PATH,0);
}

struct rb_monitor_zk *init_rbmon_zk(char *host,uint64_t pop_watcher_timeout,
  uint64_t push_timeout,json_object *zk_sensors) {
  char strerror_buf[BUFSIZ];

  assert(host);

  struct rb_monitor_zk *_zk = calloc(1,sizeof(*_zk));
  if(NULL == _zk){
    rdlog(LOG_ERR,"Can't allocate zookeeper handler (out of memory?)");
  }

#ifdef RB_MONITOR_ZK_MAGIC
  _zk->magic = RB_MONITOR_ZK_MAGIC;
#endif

  _zk->zk_host = host;
  _zk->pop_watcher_timeout = pop_watcher_timeout;
  _zk->push_timeout = push_timeout;
  _zk->zk_handler = rb_zk_init(_zk->zk_host,pop_watcher_timeout);

  rb_monitor_zk_parse_sensors(_zk,zk_sensors);

  if(NULL == _zk->zk_handler) {
    strerror_r(errno,strerror_buf,sizeof(strerror_buf));
    rdlog(LOG_ERR,"Can't init zookeeper: [%s].",strerror_buf);
    goto err;
  } else {
    rdlog(LOG_ERR,"Connected to ZooKeeper %s",_zk->zk_host);
  }

  zk_prepare(_zk->zk_handler);
  try_to_be_master(_zk);

  return _zk;
err:
  /// @TODO error treatment
  return NULL;
}
