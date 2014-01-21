// rb_system.h
#include "rb_log.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


#pragma once

static inline char * trim_end(char * buf)
{
	char * end = buf + strlen(buf)-1;
	while(end>=buf && isspace(*end))
		end--;
	*(end+1)='\0';
	return buf;
}

static inline const char *system_type_fn(void){return "system";}

/**
 Exec a system command and puts the output in value_buf
 @param worker_info    Worker_info struct
 @param value_buf      Buffer to store the output
 @param value_buf_len  Length of value_buf
 @param number         If possible, number conversion of value_buf
 @param unused         Just for snmp_solve_response compatibility
 @param command        Command to execute
 @todo see if we can join with snmp_solve_response somehow
 @return               1 if number. 0 ioc.
 */
static int system_solve_response(char * buff,const size_t buff_size,
	double * number,
	__attribute__((unused)) void * unused,const char *command)
{
	int ret=0;
	FILE * fp = popen(command, "r");
	if(NULL==fp)
	{
		Log(LOG_ERR,"Cannot get system command.");
	}
	else
	{
		if(NULL==fgets(buff, buff_size, fp))
		{
			Log(LOG_ERR,"Cannot get buffer information");
		}
		else
		{
			Log(LOG_DEBUG,"System response: %s",buff);
			trim_end(buff);
			char * endPtr;
			*number = strtod(buff,&endPtr);
			if(buff!=endPtr)
				ret++;
		}
		fclose(fp);
	}

	return ret;
}