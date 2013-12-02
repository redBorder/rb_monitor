// rb_log.c

#include "rb_log.h"

static int debug_level=-1;
static int debug_flags=0;

void Log0(const char * file,int line,const int level,char *fmt,...){
	assert(file);
	assert(line);

	if(level >= debug_level)
		return;

	if(debug_flags != 0){
		va_list ap;
		if(debug_flags & DEBUG_STDOUT)
		{
			va_start(ap, fmt);
			switch(level){
				case LOG_EMERG:
				case LOG_ALERT:
				case LOG_CRIT:
				case LOG_ERR:
				case LOG_WARNING:
					fprintf(stderr, "%s:%d->",file,line);
	    			vfprintf(stderr, fmt, ap);
	    			break;					
				case LOG_NOTICE:
				case LOG_INFO:
				case LOG_DEBUG:
					fprintf(stdout, "%s:%d->",file,line);
					vfprintf(stdout, fmt, ap);
					break;
			};
			va_end(ap);
		}

		if(debug_flags & DEBUG_SYSLOG)
		{
			va_start(ap, fmt);
			vsyslog(level, fmt, ap);
		    va_end(ap);
		}
	}
}

// Return the current debug level of worker_info
void debug_set_debug_level(int level){debug_level = level;}

// Set the output flags
void debug_set_output_flags(int flags){debug_flags = flags;}