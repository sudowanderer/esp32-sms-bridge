#include <unity.h>

#include "cellular_status_core.h"

#include <string.h>

void test_parse_csq_response_sets_signal_strength() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCsqResponse("+CSQ: 20,99\r\n", status, 1234));

  TEST_ASSERT_TRUE(status.signalKnown);
  TEST_ASSERT_EQUAL_UINT8(20, status.csqRssi);
  TEST_ASSERT_EQUAL_INT16(-73, status.rssiDbm);
  TEST_ASSERT_EQUAL_UINT8(99, status.csqBer);
  TEST_ASSERT_EQUAL_UINT32(1234, status.lastUpdatedMs);
}

void test_parse_csq_response_marks_unknown_signal() {
  CellularStatusSnapshot status = {};
  status.signalKnown = true;
  status.csqRssi = 15;
  status.rssiDbm = -83;

  TEST_ASSERT_TRUE(CellularStatusCore::parseCsqResponse("+CSQ: 99,99", status, 2000));

  TEST_ASSERT_FALSE(status.signalKnown);
  TEST_ASSERT_EQUAL_UINT8(99, status.csqRssi);
  TEST_ASSERT_EQUAL_INT16(0, status.rssiDbm);
  TEST_ASSERT_EQUAL_UINT32(2000, status.lastUpdatedMs);
}

void test_parse_csq_response_rejects_invalid_text() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_FALSE(CellularStatusCore::parseCsqResponse("OK", status, 100));
  TEST_ASSERT_FALSE(status.signalKnown);
}

void test_parse_cereg_response_sets_registered_home() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCeregResponse("+CEREG: 0,1\r\n", status, 3000));

  TEST_ASSERT_TRUE(status.registrationKnown);
  TEST_ASSERT_EQUAL_UINT8(1, status.registrationStatus);
  TEST_ASSERT_EQUAL_STRING("registered_home", status.registrationText);
  TEST_ASSERT_EQUAL_UINT32(3000, status.lastUpdatedMs);
}

void test_parse_cereg_response_sets_roaming() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCeregResponse("+CEREG: 0,5", status, 3000));

  TEST_ASSERT_TRUE(status.registrationKnown);
  TEST_ASSERT_EQUAL_UINT8(5, status.registrationStatus);
  TEST_ASSERT_EQUAL_STRING("registered_roaming", status.registrationText);
}

void test_parse_cereg_response_rejects_invalid_text() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_FALSE(CellularStatusCore::parseCeregResponse("+CSQ: 20,99", status, 100));
  TEST_ASSERT_FALSE(status.registrationKnown);
}

void test_parse_ati_response_sets_module_identity() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseAtiResponse("ATI\r\nCMCC\r\nML307A\r\nML307A-DSLN-MTSH1S00\r\nOK\r\n", status, 4000));

  TEST_ASSERT_TRUE(status.moduleInfoKnown);
  TEST_ASSERT_EQUAL_STRING("CMCC", status.manufacturer);
  TEST_ASSERT_EQUAL_STRING("ML307A", status.model);
  TEST_ASSERT_EQUAL_STRING("ML307A-DSLN-MTSH1S00", status.firmware);
  TEST_ASSERT_EQUAL_UINT32(4000, status.lastUpdatedMs);
}

void test_parse_cesq_response_sets_lte_signal_quality() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCesqResponse("+CESQ: 42,99,255,255,34,47\r\nOK\r\n", status, 5000));

  TEST_ASSERT_TRUE(status.lteSignalKnown);
  TEST_ASSERT_EQUAL_STRING("42,99,255,255,34,47", status.cesqRaw);
  TEST_ASSERT_EQUAL_INT16(-93, status.rsrpDbm);
  TEST_ASSERT_EQUAL_INT16(-25, status.rsrqDbTenths);
  TEST_ASSERT_EQUAL_STRING("fair", CellularStatusCore::rsrpQualityName(status.rsrpDbm, status.lteSignalKnown));
  TEST_ASSERT_EQUAL_UINT32(5000, status.lastUpdatedMs);
}

void test_parse_sim_identity_responses() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseImsiResponse("001010123456789\r\nOK\r\n", status, 6000));
  TEST_ASSERT_TRUE(CellularStatusCore::parseIccidResponse("+ICCID: 8901001234567890123F\r\nOK\r\n", status, 6100));

  TEST_ASSERT_EQUAL_STRING("001010123456789", status.imsi);
  TEST_ASSERT_EQUAL_STRING("8901001234567890123F", status.iccid);
  TEST_ASSERT_EQUAL_UINT32(6100, status.lastUpdatedMs);

  TEST_ASSERT_TRUE(CellularStatusCore::parseIccidResponse("AT+ICCID\r\n8901001234567890123F\r\nOK\r\n", status, 6200));
  TEST_ASSERT_EQUAL_STRING("8901001234567890123F", status.iccid);
}

void test_parse_cnum_response_uses_fallback_when_number_missing() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCnumResponse("OK\r\n", status, 7000));

  TEST_ASSERT_EQUAL_STRING("not stored or unsupported", status.ownNumber);
  TEST_ASSERT_EQUAL_UINT32(7000, status.lastUpdatedMs);
}

void test_parse_operator_pdp_and_apn_responses() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCopsResponse("+COPS: 0,2,\"00101\",7\r\nOK\r\n", status, 8000));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse("+CGDCONT: 1,\"IP\",\"EXAMPLE.APN\",\"0.0.0.0\",0,0\r\nOK\r\n", status, 8200));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgactResponse("+CGACT: 1,1\r\nOK\r\n", status, 8300));

  TEST_ASSERT_EQUAL_STRING("00101", status.operatorName);
  TEST_ASSERT_TRUE(status.dataConnectionKnown);
  TEST_ASSERT_TRUE(status.dataConnectionActive);
  TEST_ASSERT_EQUAL_STRING("EXAMPLE.APN", status.apn);
  TEST_ASSERT_EQUAL_UINT32(8300, status.lastUpdatedMs);
}

void test_parse_pdp_ignores_ims_when_traffic_context_is_inactive() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse(
      "+CGDCONT: 1,\"IPV4V6\",\"3gnet\",,0,0,,,,\r\n"
      "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\r\n"
      "OK\r\n",
      status, 8400));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgactResponse(
      "+CGACT: 1,0\r\n"
      "+CGACT: 8,1\r\n"
      "OK\r\n",
      status, 8500));

  TEST_ASSERT_TRUE(status.dataConnectionKnown);
  TEST_ASSERT_FALSE(status.dataConnectionActive);
  TEST_ASSERT_EQUAL_STRING("3gnet", status.apn);
  TEST_ASSERT_EQUAL_UINT8(2, status.pdpContextCount);
}

void test_parse_pdp_reports_active_when_traffic_context_is_active() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse(
      "+CGDCONT: 1,\"IPV4V6\",\"3gnet\",,0,0,,,,\r\n"
      "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\r\n"
      "OK\r\n",
      status, 8600));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgactResponse(
      "+CGACT: 1,1\r\n"
      "+CGACT: 8,1\r\n"
      "OK\r\n",
      status, 8700));

  TEST_ASSERT_TRUE(status.dataConnectionKnown);
  TEST_ASSERT_TRUE(status.dataConnectionActive);
  TEST_ASSERT_EQUAL_STRING("3gnet", status.apn);
}

void test_parse_pdp_context_refresh_clears_stale_activation_state() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse(
      "+CGDCONT: 1,\"IPV4V6\",\"3gnet\",,0,0,,,,\r\n"
      "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\r\n"
      "OK\r\n",
      status, 8720));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgactResponse(
      "+CGACT: 1,1\r\n"
      "+CGACT: 8,1\r\n"
      "OK\r\n",
      status, 8740));

  TEST_ASSERT_TRUE(status.dataConnectionKnown);
  TEST_ASSERT_TRUE(status.dataConnectionActive);

  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse(
      "+CGDCONT: 1,\"IPV4V6\",\"3gnet\"\r\n"
      "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\r\n"
      "OK\r\n",
      status, 8760));

  TEST_ASSERT_FALSE(status.dataConnectionKnown);
  TEST_ASSERT_FALSE(status.dataConnectionActive);
  TEST_ASSERT_EQUAL_STRING("3gnet", status.apn);
}

void test_parse_pdp_treats_only_ims_active_as_inactive_data_connection() {
  CellularStatusSnapshot status = {};

  TEST_ASSERT_TRUE(CellularStatusCore::parseCgdccontResponse(
      "+CGDCONT: 8,\"IPV4V6\",\"IMS\",,0,0,0,2,1,1\r\n"
      "OK\r\n",
      status, 8800));
  TEST_ASSERT_TRUE(CellularStatusCore::parseCgactResponse(
      "+CGACT: 8,1\r\n"
      "OK\r\n",
      status, 8900));

  TEST_ASSERT_TRUE(status.dataConnectionKnown);
  TEST_ASSERT_FALSE(status.dataConnectionActive);
  TEST_ASSERT_EQUAL_STRING("", status.apn);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_parse_csq_response_sets_signal_strength);
  RUN_TEST(test_parse_csq_response_marks_unknown_signal);
  RUN_TEST(test_parse_csq_response_rejects_invalid_text);
  RUN_TEST(test_parse_cereg_response_sets_registered_home);
  RUN_TEST(test_parse_cereg_response_sets_roaming);
  RUN_TEST(test_parse_cereg_response_rejects_invalid_text);
  RUN_TEST(test_parse_ati_response_sets_module_identity);
  RUN_TEST(test_parse_cesq_response_sets_lte_signal_quality);
  RUN_TEST(test_parse_sim_identity_responses);
  RUN_TEST(test_parse_cnum_response_uses_fallback_when_number_missing);
  RUN_TEST(test_parse_operator_pdp_and_apn_responses);
  RUN_TEST(test_parse_pdp_ignores_ims_when_traffic_context_is_inactive);
  RUN_TEST(test_parse_pdp_reports_active_when_traffic_context_is_active);
  RUN_TEST(test_parse_pdp_context_refresh_clears_stale_activation_state);
  RUN_TEST(test_parse_pdp_treats_only_ims_active_as_inactive_data_connection);
  return UNITY_END();
}
