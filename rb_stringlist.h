#ifndef RB_STRINGLIST
#define RB_STRINGLIST

//#include "librd/rdstring.h"

#define RB_STRINGLIST_STARTSIZE 64

typedef struct rb_stringlist_s{
	char ** stringlist;
	size_t count,size;
} rb_stringlist_t;

static inline rb_stringlist_t *rb_stringlist_new(void){
	return calloc(1,sizeof(rb_stringlist_t));
}


/* WARNING: String ownership not passed to stringlist. The second will not be free it in rb_stringlist_delete */
static inline int rb_stringlist_add(rb_stringlist_t *stringlist,char * string){
	if(stringlist->count == stringlist->size)
	{
		const size_t nextsize = stringlist->size ? (stringlist->size)*2 : RB_STRINGLIST_STARTSIZE;
		char ** aux = realloc(stringlist->stringlist,sizeof(char *)*nextsize);
		if(NULL == aux){
			return 0;
		}else{
			stringlist->stringlist = aux;
			stringlist->size = nextsize;
		}
	}
	stringlist->stringlist[stringlist->count++] = string;
	return 1;
}

/* @warning Not checking if idx<count!! */
static inline char* rb_stringlist_at(rb_stringlist_t *stringlist,const size_t idx)
{
	return stringlist->stringlist[idx];
}

static inline size_t rb_stringlist_count(const rb_stringlist_t *stringlist)
{
	return stringlist->count;
}


static inline void rb_stringlist_freeall(rb_stringlist_t *sl)
{
	unsigned i;
	for(i=0;i<rb_stringlist_count(sl);++i)
		free(sl->stringlist[i]);
}

static inline void rb_stringlist_delete(rb_stringlist_t *sl){
	free(sl);
}

#endif // RB_STRINGLIST
