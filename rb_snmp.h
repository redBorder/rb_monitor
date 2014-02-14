
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

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

struct monitor_snmp_new_session_config{
	const char * community;
	int timeout;
	int flags;
	long version;
};

struct monitor_snmp_session;

/** 
	Adapt the snmp response in string and double responses.
	@param value_buf   Return buffer where the response will be saved (text format)
	@param number      If possible, the response will be saved in double format here
	@param _session    SNMP session (performance reasons)
	@param _oid_string String representing oid
	@return            0 if number was not setted; non 0 otherwise.
 */

struct monitor_snmp_session * new_snmp_session(struct snmp_session *ss,const struct monitor_snmp_new_session_config *config);
int snmp_solve_response(char * value_buf,const size_t value_buf_len,
	double * number,struct monitor_snmp_session * session,const char *oid_string);
void destroy_snmp_session(struct monitor_snmp_session *);

int net_snmp_version(const char *string_version,const char *sensor_name);

static inline const char * snmp_type_fn(void){return "snmp";}
