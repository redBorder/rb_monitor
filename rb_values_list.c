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

// @todo use librd's one
static inline char * _rd_memctx_strdup(rd_memctx_t *memctx,const char *src)
{
       const size_t len = strlen(src);
       char * dst = rd_memctx_malloc(memctx,len+1);
       memcpy(dst,src,len+1);
       return dst;
}


/* Copy just the 'useful' data of the node, not list-related */
static inline void monitor_value_copy(struct monitor_value *dst,const struct monitor_value *src)
{
	dst->timestamp       = src->timestamp;
	dst->sensor_id       = src->sensor_id;
	if(src->sensor_name)
		dst->sensor_name     = _rd_memctx_strdup(&memctx,src->sensor_name);
	if(src->name)
		dst->name            = _rd_memctx_strdup(&memctx,src->name);
	if(src->send_name)
		dst->send_name       = _rd_memctx_strdup(&memctx,src->send_name);
	if(src->instance_prefix)
		dst->instance_prefix = _rd_memctx_strdup(&memctx,src->instance_prefix);
	dst->instance        = src->instance;
	dst->instance_valid  = src->instance_valid;
	dst->bad_value       = src->bad_value;
	dst->value           = src->value;
	if(src->string_value) 
		dst->string_value    = _rd_memctx_strdup(&memctx,src->string_value);
	if(src->unit) 
		dst->unit            = _rd_memctx_strdup(&memctx,src->unit);
	if(src->group_name) 
		dst->group_name      = _rd_memctx_strdup(&memctx,src->group_name);
	if(src->group_id) 
		dst->group_id        = _rd_memctx_strdup(&memctx,src->group_id);
}

/**
 Add a monitor value to the tree. 'src' will be copied and not changed.
 */
struct monitor_value * add_monitor_value(struct monitor_values_tree*tree,const struct monitor_value *src)
{
	struct monitor_value * dst = rd_memctx_calloc(&memctx,1,sizeof(struct monitor_value));
	#ifdef MONITOR_VALUE_MAGIC
	dst->magic = MONITOR_VALUE_MAGIC;
	#endif
	
	if(dst)
	{
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