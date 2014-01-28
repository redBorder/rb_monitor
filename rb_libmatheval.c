// rb_libmatheval.c

struct libmatheval_stuffs{
	const char **names;
	double *values;
	bool *isvector;
	int *vector_pos;

	unsigned int variables_pos;
	unsigned int total_lenght;
};

struct libmatheval_stuffs * new_libmatheval_stuffs(size_t size)
{
	
}

int libmatheval_append(struct libmatheval_stuffs *matheval,const char *name,double val){
	Log(LOG_DEBUG,"[libmatheval] Saving %s var in libmatheval array. Value=%.3lf\n",
						                                         name,val);

	assert(matheval);
	assert(matheval->names);
	assert(matheval->values);
	assert(matheval->isvector);
	assert(matheval->vector_pos);

	if(matheval->variables_pos<matheval->total_lenght){
	}else{
		if (NULL != (matheval->names = realloc(matheval->names,matheval->total_lenght*2*sizeof(char *)))
			&& NULL != (matheval->values = realloc(matheval->values,matheval->total_lenght*2*sizeof(double))))
		{
			matheval->total_lenght*=2;
		}
		else
		{
			Log(LOG_CRIT,"Memory error. \n",__LINE__);
			if(matheval->names) free(matheval->names);
			matheval->total_lenght = 0;
			return 0;
		}
	}

	matheval->names[matheval->variables_pos] = strdup(name);
	matheval->values[matheval->variables_pos++] = val;
	return 1;
}

int libmatheval_search_vector(const char ** variables,size_t variables_count, const char *vector, size_t *pos,size_t *size)
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


int libmatheval_check_exists(char ** vars,int varcount,const struct libmatheval_stuffs *stuffs,int *vars_pos){
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