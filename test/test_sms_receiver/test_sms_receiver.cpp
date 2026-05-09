#include <unity.h>

#include "sms_receiver_core.h"

#include <string.h>

struct TestContext {
  SmsMessage lastMessage;
  int receivedCount;
  char lastError[64];
  char lastRawLine[384];
  int errorCount;
  bool decoderShouldPass;
  bool decoderCalled;
};

static void resetContext(TestContext& ctx) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.decoderShouldPass = true;
}

static void copyText(char* dest, size_t destCapacity, const char* source) {
  strncpy(dest, source, destCapacity - 1);
  dest[destCapacity - 1] = '\0';
}

static bool fakeDecodePdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  if (!ctx->decoderShouldPass) {
    copyText(error, errorCapacity, "fake_decode_failed");
    return false;
  }

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120000");
  copyText(message->text, sizeof(message->text), "hello");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  return true;
}

static bool fakeDecodeConcatPdu(const char* rawPdu, SmsMessage* message, char* error, size_t errorCapacity, void* userData) {
  (void)error;
  (void)errorCapacity;
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->decoderCalled = true;

  copyText(message->sender, sizeof(message->sender), "+8613800138000");
  copyText(message->timestamp, sizeof(message->timestamp), "260509120001");
  copyText(message->text, sizeof(message->text), "part one");
  copyText(message->pdu, sizeof(message->pdu), rawPdu);
  message->isConcat = true;
  message->concatRef = 42;
  message->concatPart = 1;
  message->concatTotal = 2;
  return true;
}

static void captureSms(const SmsMessage& message, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->lastMessage = message;
  ctx->receivedCount++;
}

static void captureError(const char* reason, const char* rawLine, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  copyText(ctx->lastError, sizeof(ctx->lastError), reason);
  copyText(ctx->lastRawLine, sizeof(ctx->lastRawLine), rawLine);
  ctx->errorCount++;
}

void test_cmt_plus_pdu_emits_sms_message() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("0891683108200505F0", 101));

  TEST_ASSERT_TRUE(ctx.decoderCalled);
  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_EQUAL_STRING("+8613800138000", ctx.lastMessage.sender);
  TEST_ASSERT_EQUAL_STRING("260509120000", ctx.lastMessage.timestamp);
  TEST_ASSERT_EQUAL_STRING("hello", ctx.lastMessage.text);
  TEST_ASSERT_EQUAL_STRING("0891683108200505F0", ctx.lastMessage.pdu);
  TEST_ASSERT_FALSE(ctx.lastMessage.isConcat);
  TEST_ASSERT_EQUAL(0, ctx.errorCount);
}

void test_non_sms_urc_is_not_consumed() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);

  TEST_ASSERT_FALSE(core.onUrc("+CEREG: 0,1", 100));
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_non_hex_pdu_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("not-a-pdu", 101));

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_not_hex", ctx.lastError);
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_odd_length_pdu_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("ABC", 101));

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_not_hex", ctx.lastError);
  TEST_ASSERT_FALSE(ctx.decoderCalled);
}

void test_decode_failure_reports_error() {
  TestContext ctx;
  resetContext(ctx);
  ctx.decoderShouldPass = false;
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("0891683108200505F0", 101));

  TEST_ASSERT_TRUE(ctx.decoderCalled);
  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("fake_decode_failed", ctx.lastError);
}

void test_concat_sms_is_detected_but_not_merged_in_v0() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodeConcatPdu, &ctx);
  core.setReceivedCallback(captureSms, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  TEST_ASSERT_TRUE(core.onUrc("0891683108200505F0", 101));

  TEST_ASSERT_EQUAL(1, ctx.receivedCount);
  TEST_ASSERT_TRUE(ctx.lastMessage.isConcat);
  TEST_ASSERT_EQUAL(42, ctx.lastMessage.concatRef);
  TEST_ASSERT_EQUAL(1, ctx.lastMessage.concatPart);
  TEST_ASSERT_EQUAL(2, ctx.lastMessage.concatTotal);
}

void test_waiting_for_pdu_times_out() {
  TestContext ctx;
  resetContext(ctx);
  SmsReceiverCore core;
  core.begin(fakeDecodePdu, &ctx);
  core.setErrorCallback(captureError, &ctx);

  TEST_ASSERT_TRUE(core.onUrc("+CMT: ,24", 100));
  core.poll(5100);

  TEST_ASSERT_EQUAL(1, ctx.errorCount);
  TEST_ASSERT_EQUAL_STRING("pdu_timeout", ctx.lastError);
  TEST_ASSERT_EQUAL_STRING("+CMT: ,24", ctx.lastRawLine);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_cmt_plus_pdu_emits_sms_message);
  RUN_TEST(test_non_sms_urc_is_not_consumed);
  RUN_TEST(test_non_hex_pdu_reports_error);
  RUN_TEST(test_odd_length_pdu_reports_error);
  RUN_TEST(test_decode_failure_reports_error);
  RUN_TEST(test_concat_sms_is_detected_but_not_merged_in_v0);
  RUN_TEST(test_waiting_for_pdu_times_out);
  return UNITY_END();
}
