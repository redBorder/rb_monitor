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

#include "rb_values_list.h"
#include "librd/rdmem.h"

#define MONITOR_VALUES_TREE_MAGIC 0xABCDEF

static rd_memctx_t memctx;

struct monitor_values_tree{
	#ifdef MONITOR_VALUES_TREE_MAGIC
	int magic;
	#endif
	rd_avl_t * avl;
};

static int monitor_value_cmp(const void *_v1,const void*_v2)
{
	assert(_v1);
	assert(_v2);

	struct monitor_value *v1=(struct monitor_value *)_v1;
	struct monitor_value *v2=(struct monitor_value *)_v2;

	#ifdef MONITOR_VALUE_MAGIC
	assert(v1->magic==MONITOR_VALUE_MAGIC);
	assert(v2->magic==MONITOR_VALUE_MAGIC);
	#endif

	int ret = strcmp(v1->sensor_name,v2->sensor_name);
	if(0==ret && v1->group_id && v2->group_id)
		ret = strcmp(v1->group_id,v2->group_id);
	if(0==ret)
		ret = strcmp(v1->name,v2->name);
	if(0==ret && v1->group_id && v2->group_id)
		ret = strcmp(v1->group_id,v2->group_id);
	if(ret==0 && v1->instance_prefix && v2->instance_prefix) // @TODO force instance_prefix setted
		ret = v1->instance-v2->instance;
	return ret;

}

struct monitor_values_tree * new_monitor_values_tree()
{
	rd_memctx_init(&memctx,"monitor_values",RD_MEMCTX_F_LOCK | RD_MEMCTX_F_TRACK);

	struct monitor_values_tree * ret = rd_memctx_calloc(&memctx,1,sizeof(struct monitor_values_tree));
	#ifdef MONITOR_VALUES_TREE_MAGIC
	ret->magic = MONITOR_VALUES_TREE_MAGIC;
	#endif
	ret->avl = rd_avl_init(NULL,monitor_value_cmp,RD_AVL_F_LOCKS);

	return ret;
}

static inline char * _rd_memctx_strdup(rd_memctx_t *memctx,const char *src)
{
    	const size_t len = strlen(src);
    	char * dst = rd_memctx_malloc(memctx,len+1);
    	memcpy(dst,src,len+1);
    	return dst;
}

/**
 Add a monitor value to the tree. 'src' will be copied and not changed.
 */
struct monitor_value * add_monitor_value(struct monitor_values_tree*tree,const struct monitor_value *src)
{
	struct monitor_value * dst = rd_memctx_calloc(&memctx,1,sizeof(struct monitor_value));

	if(dst)
	{
		#ifdef MONITOR_VALUE_MAGIC
		dst->magic = MONITOR_VALUE_MAGIC;
		#endif

		rd_memctx_init(&dst->memctx,NULL,RD_MEMCTX_F_LOCK | RD_MEMCTX_F_TRACK);

		monitor_value_copy(dst,src);
		RD_AVL_INSERT(tree->avl,dst,avl_node);
	}

	return dst;
}

struct monitor_value * find_monitor_value(struct monitor_values_tree *tree,const struct monitor_value *node)
{
	return RD_AVL_FIND(tree->avl,node);
}

const struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,const struct monitor_value *src)
{
	struct monitor_value * current_value = find_monitor_value(tree,src);
	if(current_value)
	{
		if(src->timestamp != current_value->timestamp)
		{
			// monitor_value_copy(current_value,src); <- Not possible because strings
			current_value->timestamp = src->timestamp;
			current_value->value = src->value;
		}
		else
		{
			// fprintf(stderr,"Same timestamp, return no monitor value.\n");
			current_value = NULL;
		}
	}
	else
	{
		current_value = add_monitor_value(tree,src);
	}

	return current_value;
}

void destroy_monitor_values_tree(struct monitor_values_tree*tree)
{
	#ifdef MONITOR_VALUES_TREE_MAGIC
	assert(tree->magic=MONITOR_VALUES_TREE_MAGIC);
	#endif

	rd_avl_destroy (tree->avl);
	rd_memctx_freeall(&memctx); // tree was in this memctx. No need for free it twice.
}
