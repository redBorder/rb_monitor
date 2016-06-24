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

struct monitor_values_tree {
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

	int ret = 0;
	if(0==ret && v1->group_id && v2->group_id)
		ret = strcmp(v1->group_id,v2->group_id);
	return ret == 0 ? strcmp(v1->name,v2->name) : 0;
}

struct monitor_values_tree * new_monitor_values_tree() {
	rd_memctx_init(&memctx,"monitor_values",RD_MEMCTX_F_LOCK | RD_MEMCTX_F_TRACK);

	struct monitor_values_tree * ret = rd_memctx_calloc(&memctx,1,sizeof(struct monitor_values_tree));
	#ifdef MONITOR_VALUES_TREE_MAGIC
	ret->magic = MONITOR_VALUES_TREE_MAGIC;
	#endif
	ret->avl = rd_avl_init(NULL,monitor_value_cmp,RD_AVL_F_LOCKS);

	return ret;
}

/** Add a monitor value to the tree.
  @param tree Tree to add monitor value
  @param mv Monitor value to add
  */
void add_monitor_value(struct monitor_values_tree *tree,
					struct monitor_value *mv) {
	RD_AVL_INSERT(tree->avl,mv,avl_node);
}

struct monitor_value *find_monitor_value(struct monitor_values_tree *tree,
				const char *name, const char *group_id) {
	const struct monitor_value dummy_node = {
#ifdef MONITOR_VALUE_MAGIC
		.magic = MONITOR_VALUE_MAGIC,
#endif
		.group_id = group_id,
		.name = name,
	};

	return RD_AVL_FIND(tree->avl,&dummy_node);
}

void destroy_monitor_values_tree(struct monitor_values_tree*tree)
{
	#ifdef MONITOR_VALUES_TREE_MAGIC
	assert(tree->magic=MONITOR_VALUES_TREE_MAGIC);
	#endif

	rd_avl_destroy (tree->avl);
	rd_memctx_freeall(&memctx); // tree was in this memctx. No need for free it twice.
}
