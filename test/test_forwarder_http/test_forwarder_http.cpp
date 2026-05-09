#include <unity.h>

#include "forwarder_http_core.h"
#include "sms_queue_core.h"

#include <string.h>

static void fillMessage(SmsMessage& message, const char* sender, const char* text) {
  memset(&message, 0, sizeof(message));
  strncpy(message.sender, sender, sizeof(message.sender) - 1);
  strncpy(message.timestamp, "260510120000", sizeof(message.timestamp) - 1);
  strncpy(message.text, text, sizeof(message.text) - 1);
  strncpy(message.pdu, "0891683108200505F0", sizeof(message.pdu) - 1);
}

void test_json_escape_handles_quotes_slashes_and_control_chars() {
  char output[96];

  forwarderHttpEscapeJson("line1\n\"quoted\"\\tail", output, sizeof(output));

  TEST_ASSERT_EQUAL_STRING("line1\\n\\\"quoted\\\"\\\\tail", output);
}

void test_json_escape_truncates_with_valid_null_termination() {
  char output[10];

  forwarderHttpEscapeJson("abcdef\"ghijkl", output, sizeof(output));

  TEST_ASSERT_EQUAL('\0', output[sizeof(output) - 1]);
  TEST_ASSERT_TRUE(strlen(output) < sizeof(output));
}

void test_bark_channel_builds_post_json_request() {
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello bark");
  PushChannelConfig config = {};
  strncpy(config.bark.serverUrl, "https://api.day.app", sizeof(config.bark.serverUrl) - 1);
  strncpy(config.bark.deviceKey, "test-device-key", sizeof(config.bark.deviceKey) - 1);

  PushHttpRequest request = {};

  TEST_ASSERT_TRUE(forwarderHttpBuildBarkRequest(config.bark, message, request));

  TEST_ASSERT_EQUAL(PushHttpMethod::Post, request.method);
  TEST_ASSERT_EQUAL_STRING("https://api.day.app/test-device-key", request.url);
  TEST_ASSERT_EQUAL_STRING("application/json", request.contentType);
  TEST_ASSERT_EQUAL(ForwarderHttpCore::kHttpTimeoutMs, request.timeoutMs);
  TEST_ASSERT_NOT_NULL(strstr(request.body, "\"title\":\"SMS from +8613800138000\""));
  TEST_ASSERT_NOT_NULL(strstr(request.body, "\"body\":\"260510120000\\nhello bark\""));
  TEST_ASSERT_NOT_NULL(strstr(request.body, "\"group\":\"ESP32 SMS Bridge\""));
}

void test_bark_channel_rejects_missing_config() {
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  BarkChannelConfig config = {};
  PushHttpRequest request = {};

  TEST_ASSERT_FALSE(forwarderHttpBuildBarkRequest(config, message, request));
}

void test_forwarder_does_nothing_when_wifi_is_disconnected() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  ForwarderHttpCore forwarder;
  forwarder.begin();

  ForwarderHttpDecision decision = forwarder.prepare(false, queue, 100);

  TEST_ASSERT_FALSE(decision.shouldSend);
  TEST_ASSERT_EQUAL(1, queue.pendingCount());
  TEST_ASSERT_EQUAL(SmsQueueStatus::Pending, queue.get(0)->status);
}

void test_forwarder_prepares_next_due_sms_when_wifi_is_connected() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  ForwarderHttpCore forwarder;
  forwarder.begin();

  ForwarderHttpDecision decision = forwarder.prepare(true, queue, 100);

  TEST_ASSERT_TRUE(decision.shouldSend);
  TEST_ASSERT_NOT_NULL(decision.item);
  TEST_ASSERT_EQUAL(SmsQueueStatus::Sending, queue.get(0)->status);
}

void test_forwarder_marks_success_as_sent() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  ForwarderHttpCore forwarder;
  forwarder.begin();
  ForwarderHttpDecision decision = forwarder.prepare(true, queue, 100);

  PushHttpResult result = {};
  result.success = true;
  result.httpCode = 200;
  forwarder.complete(queue, decision.item, result, 150);

  TEST_ASSERT_EQUAL(SmsQueueStatus::Sent, queue.get(0)->status);
  TEST_ASSERT_EQUAL(0, queue.pendingCount());
}

void test_forwarder_marks_failure_for_retry_with_backoff() {
  SmsQueueCore queue;
  queue.begin();
  SmsMessage message;
  fillMessage(message, "+8613800138000", "hello");
  TEST_ASSERT_TRUE(queue.enqueue(message, 100));

  ForwarderHttpCore forwarder;
  forwarder.begin();
  ForwarderHttpDecision decision = forwarder.prepare(true, queue, 100);

  PushHttpResult result = {};
  result.success = false;
  result.httpCode = 500;
  strncpy(result.error, "http_500", sizeof(result.error) - 1);
  forwarder.complete(queue, decision.item, result, 150);

  const SmsQueueItem* item = queue.get(0);
  TEST_ASSERT_EQUAL(SmsQueueStatus::Failed, item->status);
  TEST_ASSERT_EQUAL(1, item->attemptCount);
  TEST_ASSERT_EQUAL(150 + ForwarderHttpCore::kBaseRetryDelayMs, item->nextAttemptMs);
  TEST_ASSERT_EQUAL_STRING("http_500", item->lastError);
  TEST_ASSERT_NULL(queue.acquireNext(item->nextAttemptMs - 1));
  TEST_ASSERT_NOT_NULL(queue.acquireNext(item->nextAttemptMs));
}

void test_retry_delay_uses_exponential_backoff_with_cap() {
  TEST_ASSERT_EQUAL(ForwarderHttpCore::kBaseRetryDelayMs, forwarderHttpRetryDelayMs(0));
  TEST_ASSERT_EQUAL(ForwarderHttpCore::kBaseRetryDelayMs * 2, forwarderHttpRetryDelayMs(1));
  TEST_ASSERT_EQUAL(ForwarderHttpCore::kMaxRetryDelayMs, forwarderHttpRetryDelayMs(20));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_json_escape_handles_quotes_slashes_and_control_chars);
  RUN_TEST(test_json_escape_truncates_with_valid_null_termination);
  RUN_TEST(test_bark_channel_builds_post_json_request);
  RUN_TEST(test_bark_channel_rejects_missing_config);
  RUN_TEST(test_forwarder_does_nothing_when_wifi_is_disconnected);
  RUN_TEST(test_forwarder_prepares_next_due_sms_when_wifi_is_connected);
  RUN_TEST(test_forwarder_marks_success_as_sent);
  RUN_TEST(test_forwarder_marks_failure_for_retry_with_backoff);
  RUN_TEST(test_retry_delay_uses_exponential_backoff_with_cap);
  return UNITY_END();
}
