// rb_log.h

#pragma once

#include <assert.h>
#include <syslog.h>
#include <stdarg.h>
#include <stdio.h>

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

// Return the current debug level of worker_info
void debug_set_debug_level(int level);

// Set the output flag
void debug_set_output_flags(int flags);

/** @WARNING worker_info->syslog_session_mutex locked and unlocked in this call! 
    be aware: performance, race conditions...
    */
void Log0(const char * file,int line,const int level,char *fmt,...);

#define Log(level,fmt...) Log0(__FILE__,__LINE__,level,fmt)
