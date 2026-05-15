#include <unity.h>

#include "modem_commands.h"
#include "startup_commands.h"

void test_startup_commands_disable_pdp_before_sms_setup() {
  TEST_ASSERT_EQUAL_UINT(5, StartupCommands::count());
  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), StartupCommands::at(0));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::deactivatePdpContext(), StartupCommands::at(1));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), StartupCommands::at(2));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsDirectUrcMode(), StartupCommands::at(3));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryRegistration(), StartupCommands::at(4));
}

void test_startup_commands_reject_out_of_range_index() {
  TEST_ASSERT_NULL(StartupCommands::at(StartupCommands::count()));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_startup_commands_disable_pdp_before_sms_setup);
  RUN_TEST(test_startup_commands_reject_out_of_range_index);
  return UNITY_END();
}
