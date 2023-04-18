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

#pragma once

#include "config.h"
#include <zookeeper/zookeeper.h>

/// @TODO document all callbacks localities, in what thread will be them
/// executed
struct rb_zk;
struct rb_zk *rb_zk_init(char *host, int timeout);

/**
	Create a node of zookeeper. Just a simple wrapper
	*/
int rb_zk_create_node(struct rb_zk *zk,
		      const char *path,
		      const char *value,
		      int valuelen,
		      const struct ACL_vector *acl,
		      int flags,
		      char *path_buffer,
		      int path_buffer_len);

/// Zookeeper mutex.
struct rb_zk_mutex;

/** Obtain the path of a redBorder Zookeper mutex.
	@param mutex Mutex you want to know about
	@return path
	*/
const char *rb_zk_mutex_path(struct rb_zk_mutex *mutex);

/** Say if a mutex is obtained by us or not.
	@param mutex Mutex you want to know about
	@return 1 if obtained, 0 ioc.
	*/
int rb_zk_mutex_obtained(struct rb_zk_mutex *mutex);

/** Callback called when a mutex status change (i.e. is obtained).
	It will be called in rb_zk own thread.
	@param mutex Mutex affected
	@param opaque Mutex opaque
	*/
typedef void (*rb_mutex_status_change_cb)(struct rb_zk_mutex *mutex,
					  void *opaque);

/** Callback called when a mutex have an error.
	It will be called in rb_zk own thread.
	@param rb_zk  rb_zk that owns the mutex.
	@param mutex  Mutex that caused the error.
	@param cause  Cause of the error.
	@param rc     Return code provided by Zookeeper
	@param opaque Mutex opaque.
	*/
typedef void (*rb_mutex_error_cb)(struct rb_zk *rb_zk,
				  struct rb_zk_mutex *mutex,
				  const char *cause,
				  int rc,
				  void *opaque);

/** Create a zookeeper node in a recursive way
	@param rb_zk Zookeeper handler
	@param path  Path you want to create
	@param flags Flags you want the path be created with.
	*/
int rb_zk_create_recursive_node(struct rb_zk *rb_zk,
				const char *path,
				int flags);

/// redBorder Zookeeper queue element basic type. It contains all the pop()
/// configuration
struct rb_zk_queue_element;

/** redBorder Zookeeper queue error callback.
	@param rb_zk  redBorder Zookeeper handler in what the error was caused.
	@param elm    Queue element the error is related to.
	@param cause  Cause of the error.
	@param rc     Zookeeper return code
	@param opaque Opaque provided in the queue element creation
	*/
typedef void (*rb_zk_queue_error_cb_t)(struct rb_zk *rb_zk,
				       struct rb_zk_queue_element *elm,
				       const char *cause,
				       int rc,
				       void *opaque);

/** Creates a new redBorder Zookeeper queue element
	@param rb_zk     Handler
	@param path      Queue path the element is related.
	@param error_cb  Queue error callback. It will be called in rb_zk own
   thread.
	@param data_cb   Data complete callback. It will be called in rb_zk own
   thread.
	@param delete_cb Lock deleted (and pop completed) callback.
	@param opaque    Queue element opaque
    @return New queue element.
	*/
struct rb_zk_queue_element *new_queue_element(struct rb_zk *rb_zk,
					      const char *path,
					      rb_zk_queue_error_cb_t error_cb,
					      data_completion_t data_cb,
					      void_completion_t delete_cb,
					      void *opaque);

/** Obtain the opaque member of a queue element.
	@elm Queue element
	*/
void *rb_zk_queue_element_opaque(struct rb_zk_queue_element *elm);

/** Callback of push completed (error or not)
	@param zk    redBorder Zookeeper handler
	@param path	 Path that identify a queue
	@param rc    Zookeeper return code
	@param cause Cause of the error (if any)
    */
typedef void (*rb_zk_push_callback)(struct rb_zk *zk,
				    const char *path,
				    int rc,
				    const char *cause);

/** Push an element to a Zookeeper queue
    @param zk       redBorder Zookeeper handler
    @param path     Queue path
    @param value    Value to push
    @param valuelen Length of value
    @param cb       Callback to execute at completion.
    @param opaque   Opaque of queue element.
    */
void rb_zk_queue_push(struct rb_zk *zk,
		      const char *path,
		      const char *value,
		      int valuelen,
		      string_completion_t cb,
		      void *opaque);

/** Try to obtain a mutex
    @param zk         redBorder Zookeeper handler
    @param mutex_path Path of the Zookeeper mutex you want to obtain
    @param schange_cb Callback to execute on mutex status change
    @param error_cb   Callback to execute on error.
    @param cb_opaque  Callback opaque.
    */
struct rb_zk_mutex *rb_zk_mutex_lock(struct rb_zk *zk,
				     const char *mutex_path,
				     rb_mutex_status_change_cb schange_cb,
				     rb_mutex_error_cb error_cb,
				     void *cb_opaque);

/** Release a lock.
	@param zk          redBorder Zookeeper handler
	@param rb_zk_mutex Mutex you want to release
	@param schange_cb  Callback called when mutex status change
	@param error_cb    Callback called when an error happens. In this case,
			   rb_zk_mutex will be deleted, and you have to create
   it again.
*/
void rb_zk_mutex_unlock(struct rb_zk *zk, struct rb_zk_mutex *mutex);

/** Pop an element from the queue.
	@param zk redBorder ZooKeeper handler.
	@param qelement Queue element configuration. From this moment, qelement
   is owned by zk.
	*/
void rb_zk_queue_pop_nolock(struct rb_zk *zk,
			    struct rb_zk_queue_element *qelement);

/** Pop an element from the queue.
	@param zk redBorder ZooKeeper handler.
	@param qelement Queue element configuration. From this moment, qelement
   is owned by zk.
	@param mutex Mutex to lock before do the pop.
	*/
void rb_zk_queue_pop(struct rb_zk *zk,
		     struct rb_zk_queue_element *qelement,
		     const char *mutex);

/** Release redBorder zookeeper resources
    @param zk redBorder Zookeeper handler
    */
void rb_zk_done(struct rb_zk *zk);
