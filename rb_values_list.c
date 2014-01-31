// rb_values_list.c

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

struct monitor_value * update_monitor_value(struct monitor_values_tree *tree,struct monitor_value *new_mv)
{
	return RD_AVL_INSERT(tree->avl,new_mv,avl_node);
}

void destroy_monitor_values_tree(struct monitor_values_tree*tree)
{
	#ifdef MONITOR_VALUES_TREE_MAGIC
	assert(tree->magic=MONITOR_VALUES_TREE_MAGIC);
	#endif

	rd_avl_destroy (tree->avl);
	rd_memctx_freeall(&memctx); // tree was in this memctx. No need for free it twice.
}
