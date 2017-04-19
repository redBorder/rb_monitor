/*
  Copyright (C) 2016 Eneo Tecnologia S.L.
  Author: Eugenio Perez <eupm90@gmail.com>

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Affero General Public License as
  published by the Free Software Foundation, either version 3 of the
  License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Affero General Public License for more details.

  You should have received a copy of the GNU Affero General Public License
  along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#pragma once

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <stdbool.h>

struct monitor_snmp_new_session_config{
	const char * community;
	int timeout;
	int flags;
	long version;
};

struct monitor_snmp_session;

struct monitor_snmp_session * new_snmp_session(struct snmp_session *ss,const struct monitor_snmp_new_session_config *config);

/**
  SNMP request & response adaption.
  @param value_buf   Return buffer where the response will be saved (text format)
  @param value_buf_len Buffer value_buf length
  @param number      If possible, the response will be saved in double format here
  @param _session    SNMP session to use
  @param _oid_string String representing oid
  @return            0 if number was not setted; non 0 otherwise.
 */
bool snmp_solve_response(char *value_buf, size_t value_buf_len,
	double *number, struct monitor_snmp_session *session, const char *oid_string);

void destroy_snmp_session(struct monitor_snmp_session *);

int net_snmp_version(const char *string_version,const char *sensor_name);
