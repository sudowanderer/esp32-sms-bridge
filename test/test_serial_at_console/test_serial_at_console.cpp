#include <unity.h>

#include "serial_at_console_core.h"

#include <string.h>

static SerialAtConsoleAction feedLine(SerialAtConsoleCore& core,
                                      const char* line,
                                      bool pending,
                                      char* command,
                                      size_t commandSize) {
  SerialAtConsoleAction action = SerialAtConsoleAction::None;
  for (const char* p = line; *p != '\0'; ++p) {
    action = core.onChar(*p, pending, command, commandSize);
    TEST_ASSERT_EQUAL(SerialAtConsoleAction::None, action);
  }
  return core.onChar('\n', pending, command, commandSize);
}

void test_help_line_returns_help_action() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Help, feedLine(core, "help", false, command, sizeof(command)));
}

void test_at_line_extracts_command() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Submit,
                    feedLine(core, "at AT+MIPCALL?", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT+MIPCALL?", command);
}

void test_line_is_trimmed_and_prefix_is_case_insensitive() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Submit,
                    feedLine(core, "  AT   ATI  ", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("ATI", command);
}

void test_unknown_prefix_is_invalid() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Invalid,
                    feedLine(core, "AT+MIPCALL?", false, command, sizeof(command)));
}

void test_pending_command_returns_busy_without_overwriting_command() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = "AT";

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Busy, feedLine(core, "at AT+CEREG?", true, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT", command);
}

void test_backspace_removes_previous_character() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  const char input[] = "at ATX\b?";
  SerialAtConsoleAction action = SerialAtConsoleAction::None;
  for (size_t i = 0; i < sizeof(input) - 1; ++i) {
    action = core.onChar(input[i], false, command, sizeof(command));
    TEST_ASSERT_EQUAL(SerialAtConsoleAction::None, action);
  }

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Submit, core.onChar('\n', false, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT?", command);
}

void test_unsafe_commands_are_rejected() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::RejectedUnsafe,
                    feedLine(core, "at AT+CGACT=0,1", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL(SerialAtConsoleAction::RejectedUnsafe,
                    feedLine(core, "at AT+MIPCALL=0,1", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL(SerialAtConsoleAction::RejectedUnsafe,
                    feedLine(core, "at AT+CFUN=4", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL(SerialAtConsoleAction::RejectedUnsafe,
                    feedLine(core, "at AT+CMGD=1", false, command, sizeof(command)));
}

void test_too_long_line_is_rejected_and_parser_recovers() {
  SerialAtConsoleCore core;
  core.begin();
  char command[SerialAtConsoleCore::kCommandCapacity] = {};

  for (size_t i = 0; i < SerialAtConsoleCore::kLineCapacity + 8; ++i) {
    TEST_ASSERT_EQUAL(SerialAtConsoleAction::None, core.onChar('A', false, command, sizeof(command)));
  }
  TEST_ASSERT_EQUAL(SerialAtConsoleAction::TooLong, core.onChar('\n', false, command, sizeof(command)));

  TEST_ASSERT_EQUAL(SerialAtConsoleAction::Submit, feedLine(core, "at AT", false, command, sizeof(command)));
  TEST_ASSERT_EQUAL_STRING("AT", command);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_help_line_returns_help_action);
  RUN_TEST(test_at_line_extracts_command);
  RUN_TEST(test_line_is_trimmed_and_prefix_is_case_insensitive);
  RUN_TEST(test_unknown_prefix_is_invalid);
  RUN_TEST(test_pending_command_returns_busy_without_overwriting_command);
  RUN_TEST(test_backspace_removes_previous_character);
  RUN_TEST(test_unsafe_commands_are_rejected);
  RUN_TEST(test_too_long_line_is_rejected_and_parser_recovers);
  return UNITY_END();
}
