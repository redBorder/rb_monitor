// rb_log.h

#pragma once

#include <assert.h>

#define DEBUG_STDOUT 0x1
#define DEBUG_SYSLOG 0x2

#if 0
#define LOG_EMERG       0       /* system is unusable */
#define LOG_ALERT       1       /* action must be taken immediately */
#define LOG_CRIT        2       /* critical conditions */
#define LOG_ERR         3       /* error conditions */
#define LOG_WARNING     4       /* warning conditions */
#define LOG_NOTICE      5       /* normal but significant condition */
#define LOG_INFO        6       /* informational */
#define LOG_DEBUG       7       /* debug-level messages */
#endif

struct _worker_info;

// Return the current debug level of worker_info
int worker_info_debug_level(const struct _worker_info *worker_info);

// Return the debug output flags from worker info
int worker_info_output_flags(const struct _worker_info *worker_info);

/** @WARNING worker_info->syslog_session_mutex locked and unlocked in this call! 
    be aware: performance, race conditions...
    */
static inline void Log(const struct _worker_info *worker_info, const int level,char *fmt,...){
	assert(worker_info);

	if(level >= worker_info_debug_level(worker_info))
		return;

	if(worker_info_output_flags(worker_info) != 0){
		va_list ap;
		if(worker_info_output_flags(worker_info) & DEBUG_STDOUT)
		{
			va_start(ap, fmt);
			switch(level){
				case LOG_EMERG:
				case LOG_ALERT:
				case LOG_CRIT:
				case LOG_ERR:
				case LOG_WARNING:
	    			vfprintf(stderr, fmt, ap);
	    			break;					
				case LOG_NOTICE:
				case LOG_INFO:
				case LOG_DEBUG:
					vfprintf(stdout, fmt, ap);
					break;
			};
			va_end(ap);
		}

		if(worker_info_output_flags(worker_info) & DEBUG_SYSLOG)
		{
			va_start(ap, fmt);
			vsyslog(level, fmt, ap);
		    va_end(ap);
		}
	}
}
