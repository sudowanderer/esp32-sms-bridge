#include <unity.h>

#include "modem_commands.h"

#include <string.h>

void test_static_commands_have_business_names() {
  TEST_ASSERT_EQUAL_STRING("AT", ModemCommands::attention());
  TEST_ASSERT_EQUAL_STRING("AT+CMGF=0", ModemCommands::smsPduMode());
  TEST_ASSERT_EQUAL_STRING("AT+CNMI=2,2,0,0,0", ModemCommands::smsDirectUrcMode());
  TEST_ASSERT_EQUAL_STRING("AT+CEREG?", ModemCommands::queryRegistration());
  TEST_ASSERT_EQUAL_STRING("AT+CSQ", ModemCommands::querySignal());
  TEST_ASSERT_EQUAL_STRING("ATI", ModemCommands::queryModuleInfo());
  TEST_ASSERT_EQUAL_STRING("AT+CESQ", ModemCommands::queryExtendedSignal());
  TEST_ASSERT_EQUAL_STRING("AT+CIMI", ModemCommands::queryImsi());
  TEST_ASSERT_EQUAL_STRING("AT+ICCID", ModemCommands::queryIccid());
  TEST_ASSERT_EQUAL_STRING("AT+CNUM", ModemCommands::queryOwnNumber());
  TEST_ASSERT_EQUAL_STRING("AT+COPS?", ModemCommands::queryOperator());
  TEST_ASSERT_EQUAL_STRING("AT+CGACT?", ModemCommands::queryPdpActivation());
  TEST_ASSERT_EQUAL_STRING("AT+CGDCONT?", ModemCommands::queryPdpContext());
}

void test_build_read_stored_sms_command() {
  char command[24];
  TEST_ASSERT_TRUE(ModemCommands::buildReadStoredSms(37, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT+CMGR=37", command);
}

void test_build_delete_stored_sms_command() {
  char command[24];
  TEST_ASSERT_TRUE(ModemCommands::buildDeleteStoredSms(43, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT+CMGD=43", command);
}

void test_build_command_rejects_too_small_buffer_without_partial_output() {
  char command[8];
  memset(command, 'x', sizeof(command));

  TEST_ASSERT_FALSE(ModemCommands::buildReadStoredSms(65535, command, sizeof(command)));
  TEST_ASSERT_EQUAL('\0', command[0]);
}

void test_build_command_rejects_null_output() {
  TEST_ASSERT_FALSE(ModemCommands::buildDeleteStoredSms(1, nullptr, 0));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_static_commands_have_business_names);
  RUN_TEST(test_build_read_stored_sms_command);
  RUN_TEST(test_build_delete_stored_sms_command);
  RUN_TEST(test_build_command_rejects_too_small_buffer_without_partial_output);
  RUN_TEST(test_build_command_rejects_null_output);
  return UNITY_END();
}
