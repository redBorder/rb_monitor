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

#include <librd/rdlog.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

struct libmatheval_stuffs{
	char ** names;
	double *values;
	unsigned int variables_pos;
	unsigned int total_lenght;
};

struct libmatheval_stuffs * new_libmatheval_stuffs(size_t new_size)
{
	struct libmatheval_stuffs * this = calloc(1,sizeof(struct libmatheval_stuffs));
	if(this)
	{
		this->names = calloc(new_size,sizeof(char *));
		if(NULL==this->names)
		{
			rdlog(LOG_CRIT,"Cannot allocate memory. Exiting.");
			free(this);
			this=NULL;
		}
	}

	if(this)
	{
		this->values = calloc(new_size,sizeof(double));
		if(NULL==this->values)
		{
			rdlog(LOG_CRIT,"Cannot allocate memory. Exiting.");
			free(this->names);
			free(this);
			this=NULL;
		}
		else
		{
			this->total_lenght = new_size;
		}
	}

	return this;
}

void delete_libmatheval_stuffs(struct libmatheval_stuffs *this)
{
	unsigned int i;
	for(i=0;i<this->variables_pos;++i)
		free(this->names[i]);
	free(this->names);
	free(this->values);
	free(this);
}

/*
 * Add a variable to a libmatheval_stuffs.
 * @param matheval struct to add the variable.
 * @param name     variable name
 * @param val      value to add
 * @return         1 in exit. 0 in other case (malloc error).
 */

static int libmatheval_append(struct libmatheval_stuffs *matheval,const char *name,double val){
	rdlog(LOG_DEBUG,"[libmatheval] Saving %s var in libmatheval array. Value=%.3lf",
						                                         name,val);

	if(matheval->variables_pos<matheval->total_lenght){
		assert(matheval);
		assert(matheval->names);
		assert(matheval->values);
	}else{
		if (NULL != (matheval->names = realloc(matheval->names,matheval->total_lenght*2*sizeof(char *)))
			&& NULL != (matheval->values = realloc(matheval->values,matheval->total_lenght*2*sizeof(double))))
		{
			matheval->total_lenght*=2;
		}
		else
		{
			rdlog(LOG_CRIT,"Memory error.");
			if(matheval->names) free(matheval->names);
			matheval->total_lenght = 0;
			return 0;
		}
	}

	matheval->names[matheval->variables_pos] = strdup(name);
	matheval->values[matheval->variables_pos++] = val;
	return 1;
}

static int libmatheval_search_vector(char ** variables,size_t variables_count, const char *vector, size_t *pos,size_t *size)
{
	int state = 0; /* 0: searching; 1: counting length; 2:finished */
	int ret = 0;
	*pos =0;
	*size=0;
	const size_t strlen_vector = strlen(vector);
	for(unsigned int i=0;state<2 && i<variables_count;++i)
	{
		switch(state)
		{
			case 0:
				if(strncmp(variables[i],vector,strlen_vector)==0)
				{
					state++;
					(*size)++;
					(*pos) = i;
					ret=1;
				}
				break;
			case 1:
				if(strncmp(variables[i],vector,strlen_vector)==0 && (strlen(variables[i])>strlen(vector)))
					(*size)++;
				else
					state++;
				break;
			default:
				break;
		};
	}
	return ret;
}

static inline int libmatheval_check_exists(char ** vars,int varcount,const struct libmatheval_stuffs *stuffs,int *vars_pos)
{
	int aok=1;
	int j;
	unsigned int i;
	for(j=0;aok && j<varcount;++j)
	{
		int var_exists=0;
		for(i=0;!var_exists && i<stuffs->variables_pos;++i)
			var_exists = !strcmp(stuffs->names[i],vars[j]);
		aok=var_exists;
	}
	*vars_pos = j-1;
	return aok;
}
