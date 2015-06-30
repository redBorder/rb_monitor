
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

#include <librd/rdevent.h>
#include <librd/rdmem.h>
#include <librd/rdlog.h>
#include <librd/rdavl.h>
#include <librd/rdsysqueue.h>
#include <stdint.h>
#include <zookeeper/zookeeper.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define RB_ZK_MAGIC 0xAB4598CF54L
#define RB_ZK_MUTEX_MAGIC 0xB0100CA1C0100CA1L

static const char ZOOKEEPER_LOCK_NODE_CONTENT[] = "";

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

/*
 * ZOOKEEPER MUTEX
 */

struct rb_zk_mutex {
#ifdef RB_ZK_MUTEX_MAGIC
  uint64_t magic;
#endif
  char *path;
  int i_am_leader;

  rb_mutex_status_change_cb schange_cb;
  rb_mutex_error_cb error_cb;
  void *cb_opaque;

  rd_avl_node_t avl_node;
  TAILQ_ENTRY(rb_zk_mutex) list_entry;
  struct rb_zk *context;
};

typedef TAILQ_HEAD(,rb_zk_mutex) rb_zk_mutex_list;
#define rb_zk_mutex_list_init(list) TAILQ_INIT(list)
#define rb_zk_mutex_list_foreach(i,list) TAILQ_FOREACH(i,list,list_entry)
#define rb_zk_mutex_list_foreach_safe(i,aux,list) \
                                TAILQ_FOREACH_SAFE(i,aux,list,list_entry)
#define rb_zk_mutex_list_insert(list,i) TAILQ_INSERT_TAIL(list,i,list_entry)
#define rb_zk_mutex_list_remove(list,i) TAILQ_REMOVE(list,i,list_entry)

const char *rb_zk_mutex_path(struct rb_zk_mutex *mutex) {
  return mutex->path;
}

static struct rb_zk_mutex *monitor_zc_mutex_casting(void *_ctx){
  struct rb_zk_mutex *context = _ctx;
#ifdef RB_ZK_MUTEX_MAGIC
  assert(RB_ZK_MUTEX_MAGIC == context->magic);
#endif

  return context;
}

int rb_zk_mutex_obtained(struct rb_zk_mutex *mutex) {
  return mutex->i_am_leader;
}

// Need to do this way because zookeeper imposed const, even when in it's doc
// says that we have to take care of free memory.
static struct rb_zk_mutex *monitor_zc_mutex_const_casting(const void *_ctx){
  struct rb_zk_mutex *context = NULL;
  memcpy(&context,&_ctx,sizeof(context));
  return context;
}

static void rb_zk_mutex_set_lock(struct rb_zk_mutex *lock) {
  lock->i_am_leader = 1;
}

static int rb_zk_mutex_cmp(const void *_m1,const void *_m2) {
  const struct rb_zk_mutex *m1 = monitor_zc_mutex_const_casting(_m1);
  const struct rb_zk_mutex *m2 = monitor_zc_mutex_const_casting(_m2);

  return strcmp(m1->path,m2->path);
}

struct rb_zk {
#ifdef RB_ZK_MAGIC
  uint64_t magic;
#endif
  char *zk_host;
  int zk_timeout;
  zhandle_t *handler;
  rd_thread_t *zk_thread;

  pthread_rwlock_t leaders_lock;
  rb_zk_mutex_list leaders_nodes_list;
  rd_avl_t leaders_avl;

  // Leaders added when ZK was down
  pthread_mutex_t pending_leaders_lock;
  rb_zk_mutex_list pending_leaders;
};

int rb_zk_create_node(struct rb_zk *zk,const char *path,const char *value,
    int valuelen,const struct ACL_vector *acl,int flags,char *path_buffer,
    int path_buffer_len) {

  return zoo_create(zk->handler,path,value,valuelen,acl,flags,path_buffer,
    path_buffer_len);
}

static void rb_zk_mutex_error_done(struct rb_zk *zk,struct rb_zk_mutex *mutex,
                                                   int rc,const char *error) {
  pthread_rwlock_wrlock(&zk->leaders_lock);
  RD_AVL_REMOVE_ELM(&zk->leaders_avl,mutex);
  rb_zk_mutex_list_remove(&zk->leaders_nodes_list, mutex);
  pthread_rwlock_unlock(&zk->leaders_lock);
  
  if(mutex->error_cb) {
    mutex->error_cb(zk,mutex,error,rc,mutex->cb_opaque);
  }

  // @TODO free of mutex->cb_opaque ??
  free(mutex->path);
  free(mutex);
}

static void rb_zk_mutex_done(struct rb_zk *zk,struct rb_zk_mutex *mutex) {
  mutex->error_cb = NULL; // not needed anymore
  rb_zk_mutex_error_done(zk,mutex,0,NULL);
}



/// send async reconnect signal
static void rb_monitor_zk_async_reconnect(struct rb_zk *context);

static int override_leader_node(struct rb_zk_mutex *lock,const char *leader_node) {
  if(lock->path){
    free(lock->path);
  }

  if(leader_node){
    lock->path = strdup(leader_node);
    if(NULL == lock->path) {
      rdlog(LOG_ERR,"Can't strdup string (out of memory?)");
      return -1;
    }
  } else {
    lock->path = NULL;
  }

  return 0;
}

static struct rb_zk *monitor_zc_casting(void *_ctx){
  struct rb_zk *context = _ctx;
#ifdef RB_ZK_MAGIC
  assert(RB_ZK_MAGIC == context->magic);
#endif

  return context;
}

/*
 *   MASTERING
 */

static int parent_node(char *path) {
  char *last_separator = strrchr(path,'/');
  if(last_separator)
    *last_separator = '\0';

  return NULL!=last_separator;
}

static void leader_get_children_complete(int rc, const struct String_vector *strings, 
                                                                   const void *data);

static void previous_leader_watcher(zhandle_t *zh, int type, int state, const char *path, 
                                                                      void *_mutex) {
  char buf[BUFSIZ];
  snprintf(buf,sizeof(buf),"%s",path);
  parent_node(buf);

  /* Something happens with previous leader. Check if we are the new master */
  struct rb_zk_mutex *mutex = monitor_zc_mutex_casting(_mutex);
  zoo_aget_children(mutex->context->handler,buf,0,
                       leader_get_children_complete,_mutex);

}

/// Return minimum string and inmediate less than 'my_str' strings.
static void leader_previous_string(const struct String_vector *strings,const char *my_str,
                                  const char **bef_str) {
  size_t i=0;
  const char *_my_str = strrchr(my_str,'/');
  if(_my_str) {
    _my_str++;
  } else {
    rdlog(LOG_ERR,"Can't find last '/' of our path [%s]. Exiting.",my_str);
  }

  for(i=0;i<strings->count;++i) {
    if(strings->data[i] == NULL) {
      continue;
    }

    /* Searching maximum before my_str */

    if(NULL != _my_str && NULL != bef_str) {
      const int my_str_cmp_rc = strcmp(strings->data[i],_my_str);
      if(my_str_cmp_rc < 0 && (NULL == *bef_str || strcmp(strings->data[i],*bef_str) > 0)) {
        *bef_str = strings->data[i];
      }
    }
  }
}

static void leader_get_children_complete(int rc, const struct String_vector *strings, const void *_mutex) {
  struct rb_zk_mutex *mutex = monitor_zc_mutex_const_casting(_mutex);

  if(0 != rc) {
    rdlog(LOG_ERR,"Can't get leader children list (%d)",rc);
    
    return;
  }

  const char *bef_str = NULL;
  leader_previous_string(strings,mutex->path,&bef_str);
  if(NULL == bef_str){
    // I'm the lower node -> I'm the leader here
    rb_zk_mutex_set_lock(mutex);
    mutex->schange_cb(mutex,mutex->cb_opaque);
  } else {
    char buf_path[BUFSIZ],prev_node_path[BUFSIZ];
    snprintf(buf_path,sizeof(buf_path),"%s",rb_zk_mutex_path(mutex));
    parent_node(buf_path);
    snprintf(prev_node_path,sizeof(prev_node_path),"%s/%s",buf_path,bef_str);

    struct Stat stat;
    rdlog(LOG_INFO,"I'm not the leader. Trying to watch %s",prev_node_path);
    const int wget_rc = zoo_wexists(mutex->context->handler,prev_node_path,
                          previous_leader_watcher,mutex,&stat);
    if(wget_rc != 0) {
      rb_zk_mutex_error_done(mutex->context,mutex,wget_rc,"Can't wget previous node");
    }
  }
}

static void create_mutex_node_complete(int rc, const char *leader_node, const void *_mutex) {
  char buf[BUFSIZ];
  struct rb_zk_mutex *mutex = monitor_zc_mutex_const_casting(_mutex);

/// @TODO treat this error in a different way
  if(rc != 0) {
    rb_zk_mutex_error_done(mutex->context,mutex,rc,"Couldn't create leader node.");
    return;
  }

  if(NULL == leader_node) {
    rb_zk_mutex_error_done(mutex->context,mutex,rc,"NULL path when returning");
    return;
  }

  const int override_rc = override_leader_node(mutex,leader_node);
  if(0 != override_rc) {
    rb_zk_mutex_error_done(mutex->context,mutex,rc,"Error overriding leading node");
    return;
  }

  /* Chopping entry if needed */
  /// @TODO control all snprintf output
  snprintf(buf,sizeof(buf),"%s",rb_zk_mutex_path(mutex));
  parent_node(buf);

  const int aget_children_rc = zoo_aget_children(mutex->context->handler,
              buf,0,leader_get_children_complete,mutex);

  if(aget_children_rc != 0) {
    rb_zk_mutex_error_done(mutex->context,mutex,rc,"Couldn't aget children");
  }
}

static struct rb_zk_mutex *rb_zk_mutex_create_entry(struct rb_zk *zk,const char *leader_path,
      rb_mutex_status_change_cb schange_cb,rb_mutex_error_cb error_cb, void *cb_opaque) {
  assert(zk);
  assert(leader_path);

  /// @TODO use rd_memctx_calloc to save callocs calls
  struct rb_zk_mutex *mutex = calloc(1,sizeof(*mutex));
  if(mutex == NULL) {
    return NULL;
  }

#ifdef RB_ZK_MUTEX_MAGIC
  mutex->magic = RB_ZK_MUTEX_MAGIC;
#endif
  mutex->path = strdup(leader_path);
  if(mutex->path == NULL) {
    goto err;
  }

  mutex->schange_cb = schange_cb;
  mutex->error_cb  = error_cb;
  mutex->cb_opaque = cb_opaque;
  mutex->context = zk;

  return mutex;
err:
  free(mutex);
  return NULL;
}

static void rb_zk_mutex_lock0(struct rb_zk *zk,struct rb_zk_mutex *mutex) {
  struct ACL_vector acl;
  memcpy(&acl,&ZOO_OPEN_ACL_UNSAFE,sizeof(acl));

  const int acreate_rc = zoo_acreate(zk->handler,rb_zk_mutex_path(mutex),
    ZOOKEEPER_LOCK_NODE_CONTENT,strlen(ZOOKEEPER_LOCK_NODE_CONTENT),
    &acl,ZOO_EPHEMERAL|ZOO_SEQUENCE,
    create_mutex_node_complete,mutex);

  if(ZOK != acreate_rc) {
    rb_zk_mutex_error_done(zk,mutex,acreate_rc,"Can't call acreate");
  } else {
    rdlog(LOG_DEBUG,"acreate called successfuly for path %s",rb_zk_mutex_path(mutex));
  }
}

static void zk_watcher_do_pending_locks(struct rb_zk *context) {
  struct rb_zk_mutex *i=NULL,*aux=NULL;

  if(zoo_state(context->handler) != ZOO_CONNECTED_STATE)
    return; //still can't do anything
  
  pthread_mutex_lock(&context->pending_leaders_lock);
  pthread_rwlock_wrlock(&context->leaders_lock);
  rb_zk_mutex_list_foreach_safe(i,aux,&context->pending_leaders) {
    rb_zk_mutex_list_remove(&context->pending_leaders,i);
    rb_zk_mutex_list_insert(&context->leaders_nodes_list,i);
    rb_zk_mutex_lock0(context,i);

  }
  pthread_rwlock_unlock(&context->leaders_lock);
  pthread_mutex_unlock(&context->pending_leaders_lock);
}

struct rb_zk_mutex * rb_zk_mutex_lock(struct rb_zk *zk,const char *leader_path,
  rb_mutex_status_change_cb schange_cb,rb_mutex_error_cb error_cb,void *cb_opaque) {
  
  struct rb_zk_mutex *_mutex = rb_zk_mutex_create_entry(zk,leader_path,
                                            schange_cb,error_cb,cb_opaque);
  rb_zk_mutex_list_insert(&zk->pending_leaders,_mutex);
  rd_thread_func_call1(zk->zk_thread,zk_watcher_do_pending_locks,zk);
  return _mutex;
}

static void delete_mutex_completed(int rc, const void *data) {
  struct rb_zk_mutex *mutex = monitor_zc_mutex_const_casting(data);

  rb_zk_mutex_done(mutex->context, mutex);
}

void rb_zk_mutex_unlock(struct rb_zk *zk,struct rb_zk_mutex *mutex) {

  if(zoo_state(zk->handler) == ZOO_CONNECTED_STATE) {
    if(NULL == mutex->path) {
      rdlog(LOG_ERR,"Does not seen mutex path -> cannot delete");
    } else {
      zoo_adelete(zk->handler,mutex->path,-1,delete_mutex_completed,mutex);
    }
  } else {
    // mutex already unlocked, since locks are ephimerals -> free resources
    delete_mutex_completed(0,mutex);
  } 
}

static void zk_watcher_clean_mutex(struct rb_zk *context,int type,int state) {
  char buf[BUFSIZ];
  struct rb_zk_mutex *i=NULL,*aux=NULL;
  rb_zk_mutex_list mutex_list_aux;

  snprintf(buf,sizeof(buf),"ZK disconnected [type=%s][state=%s]",
    type2String(type),state2String(state));
  
  pthread_rwlock_wrlock(&context->leaders_lock);
  mutex_list_aux = context->leaders_nodes_list;
  rb_zk_mutex_list_init(&context->leaders_nodes_list);
  rd_avl_init(&context->leaders_avl,context->leaders_avl.ravl_cmp,
                                            context->leaders_avl.ravl_flags);
  pthread_rwlock_unlock(&context->leaders_lock);

  if(type == ZOO_SESSION_EVENT && state == ZOO_EXPIRED_SESSION_STATE) {
    rdlog(LOG_ERR,"Trying to reconnect");
    rb_monitor_zk_async_reconnect(context);
  }

  // Notifying status change
  rb_zk_mutex_list_foreach_safe(i,aux,&mutex_list_aux) {
    i->i_am_leader = 0;
    i->schange_cb(i,i->cb_opaque);
    i->error_cb(i->context,i,buf,state,i->cb_opaque);

    rb_zk_mutex_done(context,i);
  }
}

/*
 *  FIFO QUEUE
 */

#define PRIVATE_QELEMENT_MAGIC 0x1AEEEEA1C1AEEEEAL
struct rb_zk_queue_element {
#ifdef PRIVATE_QELEMENT_MAGIC
  uint64_t magic;
#endif
  struct rb_zk *rb_zk;
  /// @TODO change name by queue_path
  char *path;
  char *queue_element_path;
  void *opaque;

  /// Watcher setted, but node added to list when setting, so we should 
  /// ignore it
  int should_ignore_watcher;

  rb_zk_queue_error_cb_t error_cb;
  data_completion_t data_cb;
  void_completion_t delete_cb;

  uint64_t refcnt;
};

static void rb_zk_queue_element_incref(struct rb_zk_queue_element *qelm) {
  ++qelm->refcnt;
}

static void rb_zk_queue_element_decref(struct rb_zk_queue_element *qelm) {
  if(--qelm->refcnt == 0) {
    free(qelm);
  }
}

struct rb_zk_queue_element *new_queue_element(struct rb_zk *rb_zk,
  const char *path,rb_zk_queue_error_cb_t error_cb,
  data_completion_t data_cb,void_completion_t delete_cb,void *opaque) {

  struct rb_zk_queue_element *qelm;
  rd_calloc_struct(&qelm,sizeof(*qelm),
    -1,path,&qelm->path,
    RD_MEM_END_TOKEN);

  if(NULL != qelm) {
#ifdef PRIVATE_QELEMENT_MAGIC
    qelm->magic = PRIVATE_QELEMENT_MAGIC;
#endif
    qelm->rb_zk     = rb_zk;
    qelm->opaque    = opaque;
    qelm->error_cb  = error_cb;
    qelm->data_cb   = data_cb;
    qelm->delete_cb = delete_cb;
    rb_zk_queue_element_incref(qelm);
  }
  return qelm;
}

void *rb_zk_queue_element_opaque(struct rb_zk_queue_element *elm) {
  return elm->opaque;
}

static struct rb_zk_queue_element *rb_zk_queue_element_cast(void *a) {
  struct rb_zk_queue_element *r = a;
#ifdef PRIVATE_QELEMENT_MAGIC
  assert(PRIVATE_QELEMENT_MAGIC == r->magic);
#endif
  return r;
}

static struct rb_zk_queue_element *rb_zk_queue_element_const_cast(const void *a) {
  struct rb_zk_queue_element *r = NULL;
  memcpy(&r,&a,sizeof(r));
  return rb_zk_queue_element_cast(r);
}

static const char *min_str(const struct String_vector *strings) {
  const char *_min_str = NULL;
  int i=0;

  for(i=0;i<strings->count;++i) {
    if(strings->data[i] == NULL) {
      continue;
    }

    if(NULL == _min_str || strcmp(strings->data[i],_min_str) < 0) {
      _min_str = strings->data[i];
    }
  }

  return _min_str;
}

static void rb_zk_queue_delete_completed(int rc, const void *data) {
  struct rb_zk_queue_element *qelm = rb_zk_queue_element_const_cast(data);
  if(qelm->delete_cb) {
    qelm->delete_cb(rc,qelm->opaque);
  }
  rb_zk_queue_element_decref(qelm);
}

static void rb_zk_queue_get_element(int rc, const char *value,int value_len,
  const struct Stat *stat,const void *data) {
  static const int IGNORE_NODE_VERSION = -1;
  struct rb_zk_queue_element *qelm = rb_zk_queue_element_const_cast(data);

  if(rc < 0) {
    qelm->error_cb(qelm->rb_zk,qelm,
      "Error getting queue element",rc,qelm->opaque);
    rb_zk_queue_element_decref(qelm);
  }

  if(qelm->data_cb) {
    qelm->data_cb(rc,value,value_len,stat,qelm);
  }

  const int adelete_rc = zoo_adelete(qelm->rb_zk->handler,
    qelm->queue_element_path,IGNORE_NODE_VERSION,
    rb_zk_queue_delete_completed,data);

  if(adelete_rc < 0) {
    if(qelm->error_cb) {
      qelm->error_cb(qelm->rb_zk,qelm,
        "Error calling adelete",rc,qelm->opaque);
    }
  }
}

static void rb_zk_queue_child_watcher(zhandle_t *zh, int type, int state, 
                                    const char *path, void *watcherCtx) {
  struct rb_zk_queue_element *qelm = rb_zk_queue_element_const_cast(watcherCtx);

  if(!qelm->should_ignore_watcher) {
    /// Node added to the queue.
    rb_zk_queue_pop_nolock(qelm->rb_zk,qelm);
  }
}

static void rb_zk_queue_pop_minimum0(int rc, 
  const struct String_vector *strings, const void *_data, int watcher_setted);

static void rb_zk_queue_pop_minimum_no_watcher_setted(int rc,
    const struct String_vector *strings, const void *_data) {
  rb_zk_queue_pop_minimum0(rc,strings,_data,0);
}

static void rb_zk_queue_pop_minimum_watcher_setted(int rc,
    const struct String_vector *strings, const void *_data) {
  rb_zk_queue_pop_minimum0(rc,strings,_data,1);
}

/// @TODO need an error callback too
static void rb_zk_queue_pop_minimum0(int rc, 
  const struct String_vector *strings, const void *_data, int watcher_setted) {

  char buf[BUFSIZ];
  struct rb_zk_queue_element *qelm = rb_zk_queue_element_const_cast(_data);

  if(rc < 0) {
    qelm->error_cb(qelm->rb_zk,qelm,
       "Error getting queue minimum",rc,qelm->opaque);
    return;
  }

  const char *_min_str = min_str(strings);
  if(NULL != _min_str) {
    snprintf(buf,sizeof(buf),"%s",qelm->path);
    parent_node(buf);
    asprintf(&qelm->queue_element_path,"%s/%s",buf,_min_str);

    if(watcher_setted) {
      rdlog(LOG_DEBUG,"Watcher already setted. Marking to ignore it.");
      qelm->should_ignore_watcher = 1;
    }

    rdlog(LOG_DEBUG,"Trying to get sensor %s",buf);
    const int aget_rc = zoo_aget(qelm->rb_zk->handler,buf,
                          0,rb_zk_queue_get_element,qelm);
    if(aget_rc != 0) {
      qelm->error_cb(qelm->rb_zk,qelm,
        "Can't do aget over min element queue",aget_rc,qelm->opaque);
      rb_zk_queue_element_decref(qelm);
    }
  } else if(!watcher_setted) {
    snprintf(buf,sizeof(buf),"%s",qelm->path);
    parent_node(buf);
    rdlog(LOG_DEBUG,"No more sensors to get, I will watch %s",buf);
    zoo_awget_children(qelm->rb_zk->handler,buf,
                        rb_zk_queue_child_watcher,qelm,
                        rb_zk_queue_pop_minimum_watcher_setted,qelm);
  } else {
    rdlog(LOG_DEBUG,"Still no sensor to get. Waiting the watcher event.");
  }
}

/// @TODO need an error callback too
void rb_zk_queue_pop_nolock(struct rb_zk *zk,struct rb_zk_queue_element *qelement) {

  char buf[BUFSIZ];
  struct ACL_vector acl;
  memcpy(&acl,&ZOO_OPEN_ACL_UNSAFE,sizeof(acl));

#ifdef PRIVATE_QELEMENT_MAGIC
  qelement->magic = PRIVATE_QELEMENT_MAGIC;
#endif
  qelement->rb_zk = zk;

  snprintf(buf,sizeof(buf),"%s",qelement->path);
  parent_node(buf);

  const int aget_children_rc = zoo_aget_children(zk->handler,buf,0,
                          rb_zk_queue_pop_minimum_no_watcher_setted,qelement);
  if(aget_children_rc < 0) {
    qelement->error_cb(zk,qelement,"Can't call aget.",aget_children_rc,qelement->opaque);
    rb_zk_queue_element_decref(qelement);
  }
}

/// @TODO need an error callback too
void rb_zk_queue_push(struct rb_zk *zk,const char *path,const char *value,int valuelen,
  string_completion_t create_mutex_node_complete, void *opaque) {

  struct ACL_vector acl;
  memcpy(&acl,&ZOO_OPEN_ACL_UNSAFE,sizeof(acl));

  const int acreate_rc = zoo_acreate(zk->handler,path,
    value,valuelen,
    &acl,ZOO_SEQUENCE,
    create_mutex_node_complete,opaque);

  if(ZOK != acreate_rc) {
    rdlog(LOG_ERR,"Can't call acreate to push queue %s element, rc=%d",path,acreate_rc);
  } else {
    rdlog(LOG_DEBUG,"acreate called successfuly for queue %s",path);
  }
}

/*
 *  REDBORDER MONITOR ZOOKEEPER
 */

static void zk_watcher(zhandle_t *zh, int type, int state, const char *path,
             void* _context) {

  struct rb_zk *context = monitor_zc_casting(_context);

  if(type == ZOO_SESSION_EVENT && state == ZOO_CONNECTED_STATE) {
    rdlog(LOG_DEBUG,"ZK connected. Adding pending mutex.");
    rd_thread_func_call1(context->zk_thread,zk_watcher_do_pending_locks,context);
  } else {
    rdlog(LOG_ERR,"Can't connect to ZK: [type: %d (%s)][state: %d (%s)]",
      type,type2String(type),state,state2String(state));

    zk_watcher_clean_mutex(context,type,state);
  }

  zoo_set_watcher(zh,zk_watcher);
}

int rb_zk_create_recursive_node(struct rb_zk *context,const char *node,int flags) {
  char aux_buf[strlen(node)];
  strcpy(aux_buf,node);
  int last_path_printed = 0;

  char *cursor = aux_buf+1;

  /* Have to create path recursively */
  cursor = strchr(cursor,'/');
  while(cursor || !last_path_printed) {
    if(cursor)
      *cursor = '\0';
    const int create_rc = zoo_create(context->handler,aux_buf,
      NULL /* Value */,
      0 /* Valuelen*/,
      &ZOO_OPEN_ACL_UNSAFE /* ACL */,
      flags /* flags */,
      NULL /* result path buffer */,
      0 /* result path buffer lenth */);

    if(create_rc != ZOK) {
      if(create_rc != ZNODEEXISTS)
        rdlog(LOG_ERR,"Can't create zookeeper path [%s]: %s",node,zerror(create_rc));
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
static void reset_zk_context(struct rb_zk *context) {
  rdlog(LOG_INFO,"Resetting ZooKeeper connection");
  if(context->handler) {
    const int close_rc = zookeeper_close(context->handler);
    if(close_rc != 0) {
      rdlog(LOG_ERR,"Error closing ZK connection [rc=%d]",close_rc);
    }
  }

  context->handler = zookeeper_init(context->zk_host, zk_watcher, context->zk_timeout, 0, context, 0);
}

static void rb_monitor_zk_async_reconnect(struct rb_zk *context) {
/// @TODO use client id too.
  rd_thread_func_call1(context->zk_thread,reset_zk_context,context);
}

static void*zk_ok_watcher(void *_context) {
  rd_thread_dispatch();
  return NULL;
}

struct rb_zk *rb_zk_init(char *host,int zk_timeout) {
  char strerror_buf[BUFSIZ];

  assert(host);

  struct rb_zk *_zk = calloc(1,sizeof(*_zk));
  if(NULL == _zk){
    rdlog(LOG_ERR,"Can't allocate zookeeper handler (out of memory?)");
  }

#ifdef RB_ZK_MAGIC
  _zk->magic = RB_ZK_MAGIC;
#endif

  _zk->zk_host = host;
  rd_avl_init(&_zk->leaders_avl,rb_zk_mutex_cmp,0);
  rb_zk_mutex_list_init(&_zk->leaders_nodes_list);
  rb_zk_mutex_list_init(&_zk->pending_leaders);
  _zk->zk_timeout = zk_timeout;
  reset_zk_context(_zk);
  rd_thread_create(&_zk->zk_thread, NULL, NULL, zk_ok_watcher, _zk);

  if(NULL == _zk->handler) {
    strerror_r(errno,strerror_buf,sizeof(strerror_buf));
    rdlog(LOG_ERR,"Can't init zookeeper: [%s].",strerror_buf);
  } else {
    rdlog(LOG_ERR,"Connected to ZooKeeper %s",_zk->zk_host);
  }

  return _zk;
}

void stop_zk(struct rb_zk *zk);

#endif
