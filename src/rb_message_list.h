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

#include <sys/queue.h>
#include <librdkafka/rdkafka.h>

/// Message we want to send. Re-use of librdkafka at this moment
typedef rd_kafka_message_t rb_message;

/// Message array element
typedef struct rb_message_array_s {
	TAILQ_ENTRY(rb_message_array_s) entry; ///< msgs entry
	size_t count; ///< # Messages in msgs
	rb_message msgs[]; ///< Messages
} rb_message_array_t;

/** Creates a new message array
  @param s Size of array
  @return New messages array
  */
rb_message_array_t *new_messages_array(size_t s);

/** Releases message array resources
  @param msgs Message array
  */
void message_array_done(rb_message_array_t *msgs);

/// List of message array
typedef TAILQ_HEAD(,rb_message_array) rb_message_list;
#define rb_message_list_init(msg_list) TAILQ_INIT(msg_list)
#define rb_message_list_push(msg_list, msg_array) \
	TAILQ_INSERT_TAIL(msg_list, msg_array, entry)
#define rb_message_list_remove(msg_list, msg_array) \
	TAILQ_REMOVE(msg_list, msg_array, entry)
#define rb_message_list_empty(msg_list) TAILQ_EMPTY(msg_list)
#define rb_message_list_first(msg_list) \
				((rb_message_array_t *)TAILQ_FIRST(msg_list))
