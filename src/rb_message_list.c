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

#include "rb_message_list.h"

#include <stdlib.h>

/** Creates a new message array
  @param s Size of array
  @return New messages array
  */
rb_message_array_t *new_messages_array(size_t s) {
	rb_message_array_t *ret = calloc(1,sizeof(ret) +
							s*sizeof(ret->msgs[0]));
	if (ret) {
		ret->count = s;
	}

	return ret;
}

/** Releases message array resources
  @param msgs Message array
  */
void message_array_done(rb_message_array_t *msgs) {
	free(msgs);
}
