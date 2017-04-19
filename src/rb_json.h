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

/// Convenience function to get a string chuld duplicated
static char *json_object_get_dup_string(json_object *json) {
	const char *ret = json_object_get_string(json);
	return ret ? strdup(ret) : NULL;
}

/// Convenience macro to get a string chuld duplicated
#define PARSE_CJSON_CHILD_STR(base, child_key, default_value)                  \
	PARSE_CJSON_CHILD0(base, child_key, json_object_get_dup_string,        \
		default_value)
