// rb_libmatheval.h

#pragma once

struct libmatheval_stuffs{
	const char ** names;
	double *values;
	unsigned int variables_pos;
	unsigned int total_lenght;
};

static int libmatheval_search_vector(const char ** variables,size_t variables_count, const char *vector, size_t *pos,size_t *size)
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
				if(strncmp(variables[i],vector,strlen_vector)==0)
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