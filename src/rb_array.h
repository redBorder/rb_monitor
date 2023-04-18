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

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

/** Generic array */
struct rb_array {
	size_t size;  ///< Number of elements can hold
	size_t count; ///< Count of elements
	void *elms[]; ///< Elements
};

/** Create a new array with count capacity */
struct rb_array *rb_array_new(size_t count);

/** Destroy a sensors array */
static void rb_array_done(struct rb_array *array) __attribute__((unused));
static void rb_array_done(struct rb_array *array) {
	free(array);
}

/** Checks if an array is full */
static bool rb_array_full(struct rb_array *array) __attribute__((unused));
static bool rb_array_full(struct rb_array *array) {
	return array->size == array->count;
}

/** Add an element to sensors array */
static void
rb_array_add(struct rb_array *array, void *elm) __attribute__((unused));
static void rb_array_add(struct rb_array *array, void *elm) {
	if (!rb_array_full(array)) {
		array->elms[array->count++] = elm;
	}
}
