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

#include <librd/rdlog.h>

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma once

static inline char *trim_end(char *buf) {
	char *end = buf + strlen(buf) - 1;
	while (end >= buf && isspace(*end))
		end--;
	*(end + 1) = '\0';
	return buf;
}

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
static bool system_solve_response(char *buff,
				  size_t buff_size,
				  double *number,
				  void *unused,
				  const char *command) {
	(void)unused;

	bool ret = false;
	FILE *fp = popen(command, "r");
	if (NULL == fp) {
		rdlog(LOG_ERR, "Cannot get system command.");
	} else {
		if (NULL == fgets(buff, buff_size, fp)) {
			rdlog(LOG_ERR, "Cannot get buffer information");
		} else {
			trim_end(buff);
			char *endPtr;
			*number = strtod(buff, &endPtr);
			if (buff != endPtr) {
				rdlog(LOG_DEBUG, "System response: %s for command %s", buff, command);
				ret = true;
			} else {
				rdlog(LOG_DEBUG, "invalid buffer response for %s", command);
			}
		}

		fclose(fp);
	}

	return ret;
}
