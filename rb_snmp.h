// rb_snmp.h

#pragma once

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

struct monitor_snmp_new_session_config{
	const char * community;
	int timeout;
	int flags;
};

struct monitor_snmp_session;

struct _worker_info; /* FW declaration */

/** 
	Adapt the snmp response in string and double responses.
	@param value_buf   Return buffer where the response will be saved (text format)
	@param number      If possible, the response will be saved in double format here
	@param _session    SNMP session (performance reasons)
	@param _oid_string String representing oid
	@return            0 if number was not setted; non 0 otherwise.
 */

struct monitor_snmp_session * new_snmp_session(struct snmp_session *ss,const struct monitor_snmp_new_session_config *config);
int snmp_solve_response(const struct _worker_info *worker_info, 
	char * value_buf,const size_t value_buf_len,
	double * number,struct monitor_snmp_session * session,const char *oid_string);
void destroy_snmp_session(struct monitor_snmp_session *);


