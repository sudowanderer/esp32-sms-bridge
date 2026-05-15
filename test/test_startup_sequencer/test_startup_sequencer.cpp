#include <unity.h>

#include "modem_commands.h"
#include "startup_sequencer_core.h"

void test_startup_submits_only_attention_first() {
  StartupSequencerCore core;
  core.begin(0);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), core.commandToSubmit(0));
  core.markSubmitted(0);
  TEST_ASSERT_NULL(core.commandToSubmit(1));
}

void test_startup_prioritizes_sms_setup_without_waiting_for_matready() {
  StartupSequencerCore core;
  core.begin(0);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), core.commandToSubmit(0));
  core.markSubmitted(0);
  core.complete(ModemAtResult::Ok, 10);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(10));
  core.markSubmitted(10);
  core.complete(ModemAtResult::Ok, 20);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsDirectUrcMode(), core.commandToSubmit(20));
  core.markSubmitted(20);
  core.complete(ModemAtResult::Ok, 30);
  TEST_ASSERT_EQUAL_STRING(ModemCommands::queryRegistration(), core.commandToSubmit(30));
  core.markSubmitted(30);
  core.complete(ModemAtResult::Ok, 40);

  TEST_ASSERT_TRUE(core.isComplete());
  TEST_ASSERT_NULL(core.commandToSubmit(40));
}

void test_command_timeout_retries_once_then_advances() {
  StartupSequencerCore core;
  core.begin(0);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), core.commandToSubmit(0));
  core.markSubmitted(0);
  core.complete(ModemAtResult::Timeout, 10);

  TEST_ASSERT_NULL(core.commandToSubmit(1000));
  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), core.commandToSubmit(1010));
  core.markSubmitted(1010);
  core.complete(ModemAtResult::Timeout, 1020);

  TEST_ASSERT_EQUAL_STRING(ModemCommands::smsPduMode(), core.commandToSubmit(1020));
}

void test_matready_is_recorded_but_does_not_gate_startup() {
  StartupSequencerCore core;
  core.begin(0);

  core.onUrc("+MATREADY", 5);

  TEST_ASSERT_TRUE(core.hasMatreadySeen());
  TEST_ASSERT_EQUAL_STRING(ModemCommands::attention(), core.commandToSubmit(5));
}

void test_startup_command_timeout_is_default() {
  TEST_ASSERT_EQUAL_UINT32(StartupSequencerCore::kDefaultCommandTimeoutMs,
                           StartupSequencerCore::timeoutForCommand(ModemCommands::attention()));
  TEST_ASSERT_EQUAL_UINT32(StartupSequencerCore::kDefaultCommandTimeoutMs,
                           StartupSequencerCore::timeoutForCommand(ModemCommands::smsPduMode()));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_startup_submits_only_attention_first);
  RUN_TEST(test_startup_prioritizes_sms_setup_without_waiting_for_matready);
  RUN_TEST(test_command_timeout_retries_once_then_advances);
  RUN_TEST(test_matready_is_recorded_but_does_not_gate_startup);
  RUN_TEST(test_startup_command_timeout_is_default);
  return UNITY_END();
}
