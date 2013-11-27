// rb_snmp.c

#include "rb_snmp.h"
#include "rb_log.h"
#include <assert.h>

#define SNMP_SESS_MAGIC 0x12345678

struct monitor_snmp_session{
	#ifdef SNMP_SESS_MAGIC
	int magic;
	#endif
	void * sessp;
};

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
		}
		else
		{
			// @TODO Logging not possible here. 
		}
	}
	return session;
}

int snmp_solve_response(const struct _worker_info *worker_info, 
	char * value_buf,const size_t value_buf_len,double * number,
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
	assert(response);

	if (status != STAT_SUCCESS)
	{
		Log(worker_info,LOG_ERR,"Snmp error: %s\n", 
			snmp_api_errstring(snmp_sess_session(session->sessp)->s_snmp_errno));
		//Log(worker_info,LOG_ERR,"Error in packet.Reason: %s\n",snmp_errstring(response->errstat));
	}
	else
	{
		Log(worker_info,LOG_DEBUG,"SNMP OID %s response type %d: %s\n",oid_string,value_buf);
	
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
				strncpy(value_buf,(const char *)response->variables->val.string,value_buf_len);
				// @TODO check val_len before copy string.
				value_buf[response->variables->val_len] = '\0';
				*number = strtod((const char *)response->variables->val.string,NULL);
				ret = 1;
				break;
			default:
				Log(worker_info,LOG_WARNING,"Unknow variable type %d in SNMP response. Line %d\n",response->variables->type,__LINE__);
		};
	}
	
	snmp_free_pdu(response);
	return ret;
}

void destroy_snmp_session(struct monitor_snmp_session * s)
{
	snmp_sess_close(s->sessp);
}