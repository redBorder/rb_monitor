/*
  Copyright (C) 2015 Eneo Tecnologia S.L.
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

#include "rb_snmp.h"
#include <librd/rd.h>
#include <librd/rdlog.h>
#include <assert.h>

// #define SNMP_SESS_MAGIC 0x12345678

struct monitor_snmp_session{
	#ifdef SNMP_SESS_MAGIC
	int magic;
	#endif
	void * sessp;
};

// @TODO include initial_session in config
struct monitor_snmp_session * new_snmp_session(struct snmp_session *initial_session,
 const struct monitor_snmp_new_session_config *config)
{
	struct monitor_snmp_session * session = calloc(1,sizeof(*session));
	if(session)
	{
		#ifdef SNMP_SESS_MAGIC
		session->magic=SNMP_SESS_MAGIC;
		#endif

		session->sessp = snmp_sess_open(initial_session);

		if(session->sessp)
		{
			/* just a pointer to opaque structure. It does not allocate anything */
			struct snmp_session *ss = snmp_sess_session(session->sessp);

			assert(ss);
			ss->community = (u_char *)strdup(config->community);
			ss->community_len = strlen(config->community);
			ss->timeout = config->timeout;
			ss->flags = config->flags;
			ss->version = config->version;
		}
		else
		{
			rdlog(LOG_ERR,"Failed to load SNMP session");
		}
	}
	return session;
}

int snmp_solve_response(char * value_buf,const size_t value_buf_len,double * number,
	struct monitor_snmp_session * session,const char *oid_string)
{
	#ifdef SNMP_SESS_MAGIC
	assert(session->magic==SNMP_SESS_MAGIC);
	#endif

	struct snmp_pdu *pdu=snmp_pdu_create(SNMP_MSG_GET);
	struct snmp_pdu *response=NULL;

	oid entry_oid[MAX_OID_LEN];
	size_t entry_oid_len = MAX_OID_LEN;
	read_objid(oid_string,entry_oid,&entry_oid_len);
	snmp_add_null_var(pdu,entry_oid,entry_oid_len);
	const int status = snmp_sess_synch_response(session->sessp,pdu,&response);
	/* A lot of variables. Just if we pass SNMPV3 someday.
	struct variable_list *vars;
	for(vars=response->variables; vars; vars=vars->next_variable)
		print_variable(vars->name,vars->name_length,vars);
	*/
	int ret = 0;
	assert(value_buf);
	assert(number);

	if (status != STAT_SUCCESS)
	{
		rdlog(LOG_ERR,"Snmp error: %s",
			snmp_api_errstring(snmp_sess_session(session->sessp)->s_snmp_errno));
		//rdlog(LOG_ERR,"Error in packet.Reason: %s\n",snmp_errstring(response->errstat));
	}
	else if(NULL==response)
	{
		rdlog(LOG_ERR,"No SNMP response given.");
	}
	else
	{
		rdlog(LOG_DEBUG,"SNMP OID %s response type %d: %s\n",oid_string,response->variables->type,value_buf);
		const size_t effective_len = RD_MIN(value_buf_len,response->variables->val_len);

		switch(response->variables->type) // See in /usr/include/net-snmp/types.h
		{
			case ASN_GAUGE:
			case ASN_INTEGER:
				snprintf(value_buf,value_buf_len,"%ld",*response->variables->val.integer);
				*number = *response->variables->val.integer;
				ret = 1;
				break;
			case ASN_OCTET_STR:
				/* We don't know if it's a double inside a string; We try to convert and save */
				strncpy(value_buf,(const char *)response->variables->val.string,effective_len-1);
				// @TODO check val_len before copy string.
				value_buf[effective_len] = '\0';
				*number = strtod((const char *)value_buf,NULL);
				ret = 1;
				break;
			default:
				rdlog(LOG_WARNING,"Unknow variable type %d in SNMP response. Line %d",response->variables->type,__LINE__);
		};
	}

	if(response)
		snmp_free_pdu(response);
	return ret;
}

int net_snmp_version(const char *string_version,const char *sensor_name){
	if(string_version)
	{
		if(0==strcmp(string_version,"1"))
			return SNMP_VERSION_1;
		else if(0==strcmp(string_version,"2c"))
			return SNMP_VERSION_2c;
	}

	rdlog(LOG_ERR,"Bad snmp version (%s) in sensor %s", string_version,
								sensor_name);
	return SNMP_DEFAULT_VERSION;
}

void destroy_snmp_session(struct monitor_snmp_session * s)
{
	snmp_sess_close(s->sessp);
	free(s);
}
