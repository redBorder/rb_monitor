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

#include <json-c/json.h>

#include <string.h>
#include <stdbool.h>

/** Parse a JSON child with a provided callback
  @param base Base object
  @param child_key Child's key
  @param cb Callback to use against c-json child object
  @return child parsed with cb, or default value
  */
#define PARSE_CJSON_CHILD0(base, child_key, cb, default_value) ({              \
		json_object *value = NULL;                                     \
		json_object_object_get_ex(base, child_key, &value);            \
		value ? cb(value) : default_value;                             \
	})

/// Convenience macro to parse a int64 child
#define PARSE_CJSON_CHILD_INT64(base, child_key, default_value)                \
	PARSE_CJSON_CHILD0(base, child_key, json_object_get_int64,             \
		default_value)

/// Convenience function to get a string child duplicated
static char *json_object_get_dup_string(json_object *json)
							__attribute__((unused));
static char *json_object_get_dup_string(json_object *json) {
	const char *ret = json_object_get_string(json);
	return ret ? strdup(ret) : NULL;
}

/// Convenience macro to get a string chuld duplicated
#define PARSE_CJSON_CHILD_STR(base, child_key, default_value)                  \
	PARSE_CJSON_CHILD0(base, child_key, json_object_get_dup_string,        \
		default_value)

/** Duplicate a JSON object
  @param orig Origin JSON to copy
  @return Returned JSON
  */
json_object *json_object_object_copy(/* @todo const */ json_object *orig);

/** Adds a json child to a json object
  @param root Root object to add child to
  @param key Child's key
  @param child Child to add
  @param err Error string (if any)
  @param err_size Error string buffer size
  @return true if success, false in other case. In case of error, please check
  err string
 */
bool add_json_child0(json_object *root, const char *key, json_object *child,
						char *err, size_t err_size);

/** Adds a JSON child to a JSON object with a callback to create JSON struct
  value
  @param root Json object to add the child
  @param key New child's key
  @param val New child's value
  @param err Error string (if any)
  @param err_size Error string size
  @param new_fn Callback to create new child
  @return true if success, false in other way
 */
#define ADD_JSON_CHILD(root, key, val, err, err_size, new_fn) ({               \
		json_object *child = new_fn(val);                              \
		add_json_child0(root, key, child, err, err_size);              \
	})

/** Adds a JSON string child to a JSON object
  @param root Json object to add the child
  @param key New child's key
  @param val New child's value
  @param err Error string (if any)
  @param err_size Error string size
  @return true if success, false in other way
 */
#define ADD_JSON_STRING(root, key, val, err, err_size) \
	ADD_JSON_CHILD(root, key, val, err, err_size, json_object_new_string)

/** Adds a JSON int64 child to a JSON object
  @param root Json object to add the child
  @param key New child's key
  @param val New child's value
  @param err Error string (if any)
  @param err_size Error string size
  @return true if success, false in other way
 */
#define ADD_JSON_INT64(root, key, val, err, err_size) \
	ADD_JSON_CHILD(root, key, val, err, err_size, json_object_new_int64)
