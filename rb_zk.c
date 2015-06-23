
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

#include "config.h"

#ifdef HAVE_ZOOKEEPER

#include "rb_zk.h"
#include "rb_log.h"

#include <pthread.h>
#include <string.h>
#include <errno.h>

/* Zookeeper path to save data */
static const char ZOOKEEPER_TASKS_PATH[]  = "/rb_monitor/tasks";
static const char ZOOKEEPER_LOCK_PATH[]   = "/rb_monitor/lock";
static const char ZOOKEEPER_LEADER_PATH[] = "/rb_monitor/leader";
static const char ZOOKEEPER_LEADER_LEAF_NAME[] = "leader_prop_";

static const int zk_read_timeout = 10000;

#define RB_MONITOR_ZK_MAGIC 0xb010b010b010b010L

static const char* state2String(int state){
  if (state == 0)
    return "CLOSED_STATE";
  if (state == ZOO_CONNECTING_STATE)
    return "CONNECTING_STATE";
  if (state == ZOO_ASSOCIATING_STATE)
    return "ASSOCIATING_STATE";
  if (state == ZOO_CONNECTED_STATE)
    return "CONNECTED_STATE";
  if (state == ZOO_EXPIRED_SESSION_STATE)
    return "EXPIRED_SESSION_STATE";
  if (state == ZOO_AUTH_FAILED_STATE)
    return "AUTH_FAILED_STATE";

  return "INVALID_STATE";
}

static const char* type2String(int type){
  if (type == ZOO_CREATED_EVENT)
    return "CREATED_EVENT";
  if (type == ZOO_DELETED_EVENT)
    return "DELETED_EVENT";
  if (type == ZOO_CHANGED_EVENT)
    return "CHANGED_EVENT";
  if (type == ZOO_CHILD_EVENT)
    return "CHILD_EVENT";
  if (type == ZOO_SESSION_EVENT)
    return "SESSION_EVENT";
  if (type == ZOO_NOTWATCHING_EVENT)
    return "NOTWATCHING_EVENT";

  return "UNKNOWN_EVENT_TYPE";
}

struct rb_monitor_zk {
#ifdef RB_MONITOR_ZK_MAGIC
  uint64_t magic;
#endif
  char *zk_host;
  time_t pop_watcher_timeout,push_timeout;
  zhandle_t *handler;
  pthread_t zk_thread;

  /// @TODO reset function that set to 0 this fields
  char *my_leader_node;
  int i_am_leader;

  /* Reconnect related signal */
  pthread_cond_t ntr_cond;
  pthread_mutex_t ntr_mutex;
  int need_to_reconnect;
};

/// send async reconnect signal
static void rb_monitor_zk_async_reconnect(struct rb_monitor_zk *context) {
  pthread_mutex_lock(&context->ntr_mutex);
  context->need_to_reconnect = 1;
  pthread_mutex_unlock(&context->ntr_mutex);
  pthread_cond_signal(&context->ntr_cond);
}

static int override_leader_node(struct rb_monitor_zk *context,const char *leader_node) {
  if(context->my_leader_node){
    free(context->my_leader_node);
  }

  if(leader_node){
    context->my_leader_node = strdup(leader_node);
    if(NULL == context->my_leader_node) {
      Log(LOG_ERR,"Can't strdup string (out of memory?)");
      return -1;
    }
  } else {
    context->my_leader_node = NULL;
  }

  return 0;
}

static struct rb_monitor_zk *monitor_zc_casting(void *_ctx){
  struct rb_monitor_zk *context = _ctx;
#ifdef RB_MONITOR_ZK_MAGIC
  assert(RB_MONITOR_ZK_MAGIC == context->magic);
#endif

  return context;
}

// Need to do this way because zookeeper imposed const, even when in it's doc
// says that we have to take care of free memory.
static struct rb_monitor_zk *monitor_zc_const_casting(const void *_ctx){
  struct rb_monitor_zk *context = NULL;
  memcpy(&context,&_ctx,sizeof(context));
  return context;
}

/*
 *   MASTERING
 */

static void leader_get_children_complete(int rc, const struct String_vector *strings, 
                                                                   const void *data);

static void previous_leader_watcher(zhandle_t *zh, int type, int state, const char *path, 
                                                                      void *_context) {
  struct rb_monitor_zk *context = monitor_zc_casting(_context);
  /* Something happens with previous leader. Check if we are the new master */
  zoo_aget_children(context->handler,ZOOKEEPER_LEADER_PATH,0,
                       leader_get_children_complete,context);

}

/// Return minimum string and inmediate less than 'my_str' strings.
static void leader_useful_string(const struct String_vector *strings,const char *my_str,
                                  const char **bef_str) {
  size_t i=0;
  const char *_my_str = strrchr(my_str,'/');
  if(_my_str) {
    _my_str++;
  } else {
    Log(LOG_ERR,"Can't find last '/' of our path [%s]. Exiting.",my_str);
  }


  for(i=0;i<strings->count;++i) {
    if(strings->data[i] == NULL) {
      continue;
    }

    /* Searching minimum */
#if 0
    if(min && NULL == *min) {
      *min = strings->data[i];
    } else {
      const int cmp_rc = strcmp(strings->data[i],*min);

      if(cmp_rc < 0){
        *min = strings->data[i];
      }
    }
#endif

    /* Searching maximum before my_str */

    if(NULL != _my_str && NULL != bef_str) {
      const int my_str_cmp_rc = strcmp(strings->data[i],_my_str);
      if(my_str_cmp_rc < 0 && (NULL == *bef_str || strcmp(strings->data[i],*bef_str) > 0)) {
        *bef_str = strings->data[i];
      }
    }
  }
}

static void leader_get_children_complete(int rc, const struct String_vector *strings, const void *data) {
  struct rb_monitor_zk *context = monitor_zc_const_casting(data);

  if(0 != rc) {
    Log(LOG_ERR,"Can't get leader children list (%d)",rc);
    
    return;
  }

  const char *bef_str = NULL;
  leader_useful_string(strings,context->my_leader_node,&bef_str);
  if(NULL == bef_str){
    Log(LOG_INFO,"I'm the leader here");
    context->i_am_leader = 1;
  } else {
    char buf[BUFSIZ];
    snprintf(buf,sizeof(buf),"%s/%s",ZOOKEEPER_LEADER_PATH,bef_str);

    struct Stat stat;
    Log(LOG_INFO,"I'm not the leader. Trying to watch %s",buf);
    const int wget_rc = zoo_wexists(context->handler,buf,
                          previous_leader_watcher,context,&stat);
    if(wget_rc != 0) {
      Log(LOG_ERR,"Can't wget [rc = %d]",wget_rc);
      rb_monitor_zk_async_reconnect(context);
    }
  }
}

static void create_master_node_complete(int rc, const char *leader_node, const void *_context) {
  struct rb_monitor_zk *context = monitor_zc_const_casting(_context);

  if(rc != 0) {
    Log(LOG_ERR,"Couldn't create leader node. rc = %d",rc);
    rb_monitor_zk_async_reconnect(context);
    return;
  }

  if(NULL == leader_node) {
    Log(LOG_ERR,"NULL path when returning");
    rb_monitor_zk_async_reconnect(context);
    return;
  }

  const int override_rc = override_leader_node(context,leader_node);
  if(0 != override_rc) {
    Log(LOG_ERR,"Error overriding leading node");
    rb_monitor_zk_async_reconnect(context);
    return;
  }

  Log(LOG_DEBUG,"ZK connected. Trying to be the leader.");

  zoo_aget_children(context->handler,ZOOKEEPER_LEADER_PATH,0,
                       leader_get_children_complete,context);
}

static void try_to_be_master(zhandle_t *zh,struct rb_monitor_zk *_context) {
  char path[BUFSIZ];
  struct ACL_vector acl;
  struct rb_monitor_zk *context = monitor_zc_casting(_context);

  snprintf(path,sizeof(path),"%s/%s",ZOOKEEPER_LEADER_PATH,ZOOKEEPER_LEADER_LEAF_NAME);
  memcpy(&acl,&ZOO_OPEN_ACL_UNSAFE,sizeof(acl));

  const int acreate_rc = zoo_acreate(zh,path,"",0,&acl,ZOO_EPHEMERAL|ZOO_SEQUENCE,
    create_master_node_complete,_context);
  if(ZOK != acreate_rc) {
    Log(LOG_ERR,"Can't call acreate (%d)",acreate_rc);
    rb_monitor_zk_async_reconnect(context);
  }
}

static void zk_watcher(zhandle_t *zh, int type, int state, const char *path,
             void* _context) {

  struct rb_monitor_zk *context = monitor_zc_casting(_context);

  if(type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE){
    Log(LOG_DEBUG,"ZK connected. Trying to be the leader.");
    context->need_to_reconnect = 0;
    try_to_be_master(zh,context);
  } else {
    Log(LOG_ERR,"Can't connect to ZK: [type: %d (%s)][state: %d (%s)]",
      type,type2String(type),state,state2String(state));
    if(type == ZOO_SESSION_EVENT && state == ZOO_EXPIRED_SESSION_STATE) {
      Log(LOG_ERR,"Trying to reconnect");
      rb_monitor_zk_async_reconnect(context);
    }
  }

  zoo_set_watcher(zh,zk_watcher);
}

static int zk_create_node(zhandle_t *zh,const char *node,int flags) {
  char aux_buf[strlen(node)];
  strcpy(aux_buf,node);
  int last_path_printed = 0;

  char *cursor = aux_buf+1;

  /* Have to create path recursively */
  cursor = strchr(cursor,'/');
  while(cursor || !last_path_printed) {
    if(cursor)
      *cursor = '\0';
    const int create_rc = zoo_create(zh,aux_buf,
      NULL /* Value */,
      0 /* Valuelen*/,
      &ZOO_OPEN_ACL_UNSAFE /* ACL */,
      flags /* flags */,
      NULL /* result path buffer */,
      0 /* result path buffer lenth */);

    if(create_rc != ZOK) {
      if(create_rc != ZNODEEXISTS)
        Log(LOG_ERR,"Can't create zookeeper path [%s]: %s",node,zerror(create_rc));
    }

    if(cursor) {
      *cursor = '/';
      cursor = strchr(cursor+1,'/');
    } else {
      last_path_printed = 1;
    }
  }

  return 1;
}

/// @TODO use client_id
static void reset_zk_context(struct rb_monitor_zk *context) {
  Log(LOG_INFO,"Resetting ZooKeeper connection");
  if(context->handler) {
    const int close_rc = zookeeper_close(context->handler);
    if(close_rc != 0) {
      Log(LOG_ERR,"Error closing ZK connection [rc=%d]",close_rc);
    }
  }

  context->handler = zookeeper_init(context->zk_host, zk_watcher, zk_read_timeout, 0, context, 0);
  context->need_to_reconnect = 0;
}

/// @TODO use client id too.
static void*zk_ok_watcher(void *_context) {
  struct rb_monitor_zk *context = monitor_zc_casting(_context);

  while(1){
    struct timespec ts = {
      .tv_sec = 1, .tv_nsec=0
    };

    /// @TODO be able to exit from here
    if(context->need_to_reconnect)
      reset_zk_context(context);
    pthread_cond_timedwait(&context->ntr_cond,&context->ntr_mutex,&ts);
  }
}

/* Prepare zookeeper structure */
static int zk_prepare(zhandle_t *zh) {
  Log(LOG_DEBUG,"Preparing zookeeper structure");
  zk_create_node(zh,ZOOKEEPER_TASKS_PATH,0);
  zk_create_node(zh,ZOOKEEPER_LOCK_PATH,0);
  zk_create_node(zh,ZOOKEEPER_LEADER_PATH,0);
}

struct rb_monitor_zk *init_zk(char *host,uint64_t pop_watcher_timeout,
  uint64_t push_timeout,json_object *zk_sensors) {
  char strerror_buf[BUFSIZ];

  assert(host);

  struct rb_monitor_zk *_zk = calloc(1,sizeof(*_zk));
  if(NULL == _zk){
    Log(LOG_ERR,"Can't allocate zookeeper handler (out of memory?)");
  }

#ifdef RB_MONITOR_ZK_MAGIC
  _zk->magic = RB_MONITOR_ZK_MAGIC;
#endif

  _zk->zk_host = host;
  _zk->pop_watcher_timeout = pop_watcher_timeout;
  _zk->push_timeout = push_timeout;
  pthread_cond_init(&_zk->ntr_cond,NULL);
  pthread_mutex_init(&_zk->ntr_mutex,NULL);
  reset_zk_context(_zk);
  pthread_create(&_zk->zk_thread, NULL, zk_ok_watcher, _zk);


  if(NULL == _zk->handler) {
    strerror_r(errno,strerror_buf,sizeof(strerror_buf));
    Log(LOG_ERR,"Can't init zookeeper: [%s].",strerror_buf);
  } else {
    Log(LOG_ERR,"Connected to ZooKeeper %s",_zk->zk_host);
  }
  zk_prepare(_zk->handler);
}

void stop_zk(struct rb_monitor_zk *zk);

#endif
