#include <unity.h>

#include "logger_core.h"

#include <stdio.h>
#include <string.h>

void test_logger_starts_empty() {
  LoggerCore logger;
  logger.begin();

  TEST_ASSERT_EQUAL(0, logger.count());
  TEST_ASSERT_EQUAL(LoggerCore::kCapacity, logger.capacity());
  TEST_ASSERT_NULL(logger.get(0));
}

void test_logger_stores_message_level_time_and_sequence() {
  LoggerCore logger;
  logger.begin();

  logger.write(LoggerLevel::Info, "wifi_status=connected", 1234);

  TEST_ASSERT_EQUAL(1, logger.count());
  const LoggerEntry* entry = logger.get(0);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL(LoggerLevel::Info, entry->level);
  TEST_ASSERT_EQUAL(1234, entry->timeMs);
  TEST_ASSERT_EQUAL(1, entry->sequence);
  TEST_ASSERT_EQUAL_STRING("wifi_status=connected", entry->message);
}

void test_logger_supports_all_level_names() {
  TEST_ASSERT_EQUAL_STRING("DEBUG", loggerLevelName(LoggerLevel::Debug));
  TEST_ASSERT_EQUAL_STRING("INFO", loggerLevelName(LoggerLevel::Info));
  TEST_ASSERT_EQUAL_STRING("WARN", loggerLevelName(LoggerLevel::Warn));
  TEST_ASSERT_EQUAL_STRING("ERROR", loggerLevelName(LoggerLevel::Error));
}

void test_logger_overwrites_oldest_entries_and_reads_oldest_first() {
  LoggerCore logger;
  logger.begin();

  for (uint16_t i = 0; i < LoggerCore::kCapacity + 3; ++i) {
    char message[32];
    snprintf(message, sizeof(message), "event=%u", i);
    logger.write(LoggerLevel::Info, message, 1000 + i);
  }

  TEST_ASSERT_EQUAL(LoggerCore::kCapacity, logger.count());
  TEST_ASSERT_EQUAL_STRING("event=3", logger.get(0)->message);
  TEST_ASSERT_EQUAL_STRING("event=4", logger.get(1)->message);
  TEST_ASSERT_EQUAL_STRING("event=102", logger.get(LoggerCore::kCapacity - 1)->message);
  TEST_ASSERT_EQUAL(LoggerCore::kCapacity + 3, logger.get(LoggerCore::kCapacity - 1)->sequence);
}

void test_logger_clear_removes_entries_and_keeps_next_sequence_monotonic() {
  LoggerCore logger;
  logger.begin();

  logger.write(LoggerLevel::Warn, "first", 100);
  logger.write(LoggerLevel::Error, "second", 200);
  logger.clear();
  logger.write(LoggerLevel::Info, "third", 300);

  TEST_ASSERT_EQUAL(1, logger.count());
  TEST_ASSERT_EQUAL_STRING("third", logger.get(0)->message);
  TEST_ASSERT_EQUAL(3, logger.get(0)->sequence);
}

void test_logger_truncates_long_messages_safely() {
  LoggerCore logger;
  logger.begin();

  char longMessage[LoggerCore::kMessageCapacity + 40];
  memset(longMessage, 'A', sizeof(longMessage));
  longMessage[sizeof(longMessage) - 1] = '\0';

  logger.write(LoggerLevel::Error, longMessage, 500);

  const LoggerEntry* entry = logger.get(0);
  TEST_ASSERT_NOT_NULL(entry);
  TEST_ASSERT_EQUAL('\0', entry->message[LoggerCore::kMessageCapacity - 1]);
  TEST_ASSERT_EQUAL(LoggerCore::kMessageCapacity - 1, strlen(entry->message));
}

void test_logger_handles_null_message_as_empty_string() {
  LoggerCore logger;
  logger.begin();

  logger.write(LoggerLevel::Info, nullptr, 600);

  TEST_ASSERT_EQUAL_STRING("", logger.get(0)->message);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_logger_starts_empty);
  RUN_TEST(test_logger_stores_message_level_time_and_sequence);
  RUN_TEST(test_logger_supports_all_level_names);
  RUN_TEST(test_logger_overwrites_oldest_entries_and_reads_oldest_first);
  RUN_TEST(test_logger_clear_removes_entries_and_keeps_next_sequence_monotonic);
  RUN_TEST(test_logger_truncates_long_messages_safely);
  RUN_TEST(test_logger_handles_null_message_as_empty_string);
  return UNITY_END();
}
