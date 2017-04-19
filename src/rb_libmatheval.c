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

#include "rb_libmatheval.h"

#include <librd/rdlog.h>

#include <stdlib.h>

struct libmatheval_vars *new_libmatheval_vars(size_t new_size) {
	struct libmatheval_vars *this = NULL;
	const size_t alloc_size = sizeof(*this)
		+ new_size*sizeof(this->names[0])
		+ new_size*sizeof(this->values[0]);

	this = calloc(1,alloc_size);
	if (NULL == this) {
		rdlog(LOG_ERR, "Cannot allocate memory. Exiting.");
	} else {
		this->names = (void *)&this[1];
		this->values = (void *)&this->names[new_size];
	}

	return this;
}

void delete_libmatheval_vars(struct libmatheval_vars *this) {
	free(this);
}
