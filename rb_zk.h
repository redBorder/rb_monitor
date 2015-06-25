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

#pragma once

#include "config.h"
#include <zookeeper/zookeeper.h>

struct rb_zk;
struct rb_zk *rb_zk_init(char *host,int timeout);

int rb_zk_create_node(struct rb_zk *zk,const char *path,const char *value,
    int valuelen,const struct ACL_vector *acl,int flags,char *path_buffer,
    int path_buffer_len);

/// @TODO change it for leader struct
struct rb_zk_mutex;
const char *rb_zk_mutex_path(struct rb_zk_mutex *mutex);
int rb_zk_mutex_obtained(struct rb_zk_mutex *mutex);
typedef void (*rb_mutex_status_change_cb)(struct rb_zk_mutex *mutex,void *opaque);
typedef void (*rb_mutex_error_cb)(struct rb_zk *rb_zk, struct rb_zk_mutex *mutex,
	const char *cause,int rc,void *opaque);

/// @TODO is this needed??
int rb_zk_create_recursive_node(struct rb_zk *zk,const char *path,int flags);

/** Try to obtain a lock.
	@param zk redBorder Zookeeper handler
	@param leader_path path you want to be the leader
	@param schange_cb Callback called when mutex status change
	@param error_cb Callback called when an error happens. In this case,
	       rb_zk_mutex will be deleted, and you have to create it again.
*/
void rb_zk_mutex_lock(struct rb_zk *zk,const char *leader_path,
	rb_mutex_status_change_cb schange_cb,rb_mutex_error_cb error_cb,void *cb_opaque);

void rb_zk_done(struct rb_zk *zk);


