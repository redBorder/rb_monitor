
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