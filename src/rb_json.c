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

#include "rb_json.h"

#include <librd/rdlog.h>

static void json_object_copy0_childs(json_object *dst,
					/* @todo const */json_object *orig) {
	for (struct json_object_iterator i = json_object_iter_begin(orig),
					end = json_object_iter_end(orig);
					!json_object_iter_equal(&i, &end);
					json_object_iter_next(&i)) {

		const char *key = json_object_iter_peek_name(&i);
		json_object *val = json_object_iter_peek_value(&i);
		json_object_get(val);
		/// @todo check add return
		json_object_object_add(dst, key, val);
	}
}

json_object *json_object_object_copy(json_object *orig) {
	if (json_object_get_type(orig) != json_type_object) {
		return NULL;
	}

	json_object *ret = json_object_new_object();
	if (ret) {
		json_object_copy0_childs(ret, orig);
	} else {
		rdlog(LOG_CRIT, "Couldn't allocate new object (OOM?)");
	}

	return ret;
}

bool add_json_child0(json_object *root, const char *key,
			json_object *child, char *err, size_t err_size) {
	if (NULL == child) {
		snprintf(err, err_size,
				"Couldn't allocate child memory (OOM?)");
		return false;
	}

	/// @todo check return value in modern versions of json-c
	json_object_object_add(root, key, child);
	#if 0
	if (add_rc < 0) {
		snprintf(err, err_size, "Couldn't add to root json");
		json_object_put(child);
		return false;
	}
	#endif
	return true;
}
