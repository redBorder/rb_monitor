#include "config.h"

#include "json_test.h"
#include "sensor_test.h"

#include <librd/rd.h>
#include <librd/rdfloat.h>

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>

#include <setjmp.h> // Needs to be before of cmocka.h

#include <cmocka.h>

#include <stdarg.h>
#include <string.h>

static const uint16_t snmp_port = 161;

// clang-format off

static const char basic_sensor[] = "{\n"
	"\"sensor_id\":1,\n"
	"\"sensor_name\": \"sensor-arriba\",\n"
	"\"sensor_ip\": \"localhost\",\n"
	"\"community\" : \"public\",\n"
	"\"timeout\": 2,"
	"\"monitors\": /* this field MUST be the last! */\n"
	"[\n"
		"{\"name\": \"integer\", \"oid\":  \"1.3.6.1.4.1.39483.1\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"gauge\", \"oid\": \"1.3.6.1.4.1.39483.2\","
					" \"unit\": \"%\", \"send\": 1},\n"
		"{\"name\": \"string\", \"oid\": \"1.3.6.1.4.1.39483.3\","
					"\"send\": 1},\n"
	"]\n"
	"}";

#define TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)                            \
	CHILD_I("sensor_id",1,                                                 \
	CHILD_S("sensor_name","sensor-arriba",                                 \
	CHILD_S("monitor",mmonitor,                                            \
	mvalue_type("value",mvalue,                                            \
	CHILD_S("type","snmp",NULL)))))

#define TEST1_CHECKS0(mmonitor,mvalue_type,mvalue)                             \
	CHILD_S("unit","%",                                                    \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue))

#define TEST1_CHECKS0_NOUNIT0(mmonitor,mvalue_type,mvalue)                     \
	TEST1_CHECKS00(mmonitor,mvalue_type,mvalue)

#define TEST1_CHECKS0_NOUNIT(mmonitor,mvalue) \
			TEST1_CHECKS0_NOUNIT0(mmonitor,CHILD_S,mvalue)

#define TEST1_CHECKS(mmonitor,mvalue) TEST1_CHECKS0(mmonitor,CHILD_S,mvalue)

static void prepare_test_basic_sensor_checks(check_list_t *check_list) {
	json_key_test checks[] = {
		JSON_KEY_TEST(TEST1_CHECKS("integer","1.000000")),
		JSON_KEY_TEST(TEST1_CHECKS("gauge","2.000000")),
		JSON_KEY_TEST(TEST1_CHECKS0_NOUNIT("string","3.000000")),
	};

	check_list_push_checks(check_list, checks, RD_ARRAYSIZE(checks));

}

/** Basic test */
TEST_FN(test_basic_sensor, prepare_test_basic_sensor_checks, basic_sensor)

int main(void) {
	const struct CMUnitTest tests[] = {
		cmocka_unit_test(test_basic_sensor),
	};

	return cmocka_run_group_tests(tests, NULL, NULL);
}

/**
 * @param type Type of response
 * @param oid Response oid
 * @param oid_len oid len
 * @param val Response value
 * @param val_size Response value size
 * @return New allocated PDU
 */
static struct snmp_pdu *snmp_sess_create_response(u_char type, const oid *oid,
			size_t oid_len, const void *val, size_t val_size) {
	struct snmp_pdu *response = calloc(1, sizeof(*response));
	response->variables = calloc(1, sizeof(*response->variables)
								+ val_size);
	response->variables->type = type;
	// All val union members are pointers so we use one of them
	response->variables->val.objid = (void *)(&response->variables[1]);
	response->variables->val_len = val_size;
	memcpy(response->variables->val.objid, val, val_size);

	return response;
}

int snmp_sess_synch_response(void *sessp, struct snmp_pdu *pdu,
						struct snmp_pdu **response) {
	static const long integers[] = {1,2};
	static const char OID_3_STR[] = "3\n";
	static const oid EXPECTED_OID_PREFIX[] = {1,3,6,1,4,1,39483};

	(void)sessp;
	assert_true(pdu->variables[0].name_length =
					RD_ARRAYSIZE(EXPECTED_OID_PREFIX) + 1);
	assert_true(0 == memcmp(EXPECTED_OID_PREFIX, pdu->variables[0].name,
						sizeof(EXPECTED_OID_PREFIX)));

	const size_t oid_suffix_pos = RD_ARRAYSIZE(EXPECTED_OID_PREFIX);
	const oid snmp_pdu_requested = pdu->variables[0].name[oid_suffix_pos];


#define SNMP_RES_CASE(res_oid_suffix, type, res_size, res)                     \
	case res_oid_suffix: *response = snmp_sess_create_response(type,       \
		pdu->variables[0].name, pdu->variables[0].name_length,         \
		res, res_size); snmp_free_pdu(pdu); return STAT_SUCCESS;


	switch(snmp_pdu_requested) {
		SNMP_RES_CASE(1, ASN_GAUGE,   sizeof(integers[0]), &integers[0])
		SNMP_RES_CASE(2, ASN_INTEGER, sizeof(integers[1]), &integers[1])
		SNMP_RES_CASE(3, ASN_OCTET_STR, strlen(OID_3_STR), OID_3_STR)
		default:
			snmp_free_pdu(pdu);
			return STAT_ERROR;
			// @todo return STAT_TIMEOUT
	};
}

void snmp_free_pdu(struct snmp_pdu *pdu) {
	free(pdu->variables);
	free(pdu);
}
