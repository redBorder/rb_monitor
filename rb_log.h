
/*
** Copyright (C) 2014 Eneo Tecnologia S.L.
** Author: Eugenio Perez <eupm90@gmail.com>
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License Version 2 as
** published by the Free Software Foundation.  You may not use, modify or
** distribute this program under any other version of the GNU General
** Public License.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
*/

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
