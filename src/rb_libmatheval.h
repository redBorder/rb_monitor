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

#include <stdbool.h>
#include <string.h>

/** Libmatheval needed variables */
struct libmatheval_vars {
	char **names; ///< Variable names
	double *values; ///< Variable values
	size_t count;
};

/** Create a new libmatheval vars
  @param new_size # vars it can hold
  @return New libmatheval vars
  */
struct libmatheval_vars *new_libmatheval_vars(size_t new_size);

/** Deallocate libmatheval vars
  @param this libmatheval vars to deallocate
  */
void delete_libmatheval_vars(struct libmatheval_vars *this);
