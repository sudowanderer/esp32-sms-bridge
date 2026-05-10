#include <unity.h>

#include "modem_at_core.h"

#include <string.h>

struct TestContext {
  char writes[256];
  size_t writeLen;
  ModemAtResult lastResult;
  char response[1024];
  int callbackCount;
  char urcs[8][1536];
  int urcCount;
};

static void resetContext(TestContext& ctx) {
  memset(&ctx, 0, sizeof(ctx));
  ctx.lastResult = ModemAtResult::Timeout;
}

static void captureWrite(const char* data, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  const size_t len = strlen(data);
  if (ctx->writeLen + len + 1 >= sizeof(ctx->writes)) {
    return;
  }

  memcpy(ctx->writes + ctx->writeLen, data, len);
  ctx->writeLen += len;
  ctx->writes[ctx->writeLen] = '\0';
}

static void captureAtResult(ModemAtResult result, const char* response, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  ctx->lastResult = result;
  strncpy(ctx->response, response, sizeof(ctx->response) - 1);
  ctx->callbackCount++;
}

static void captureUrc(const char* line, void* userData) {
  TestContext* ctx = static_cast<TestContext*>(userData);
  if (ctx->urcCount >= 8) {
    return;
  }

  strncpy(ctx->urcs[ctx->urcCount], line, sizeof(ctx->urcs[ctx->urcCount]) - 1);
  ctx->urcCount++;
}

static void feedLine(ModemAtCore& core, const char* line) {
  for (const char* p = line; *p != '\0'; ++p) {
    core.onByte(*p);
  }
  core.onByte('\r');
  core.onByte('\n');
}

void test_command_starts_and_ok_finishes() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT", 1000, captureAtResult, &ctx));
  core.poll(10, captureWrite, &ctx);

  TEST_ASSERT_TRUE(core.isBusy());
  TEST_ASSERT_EQUAL_STRING("AT\r\n", ctx.writes);

  feedLine(core, "OK");

  TEST_ASSERT_FALSE(core.isBusy());
  TEST_ASSERT_EQUAL(1, ctx.callbackCount);
  TEST_ASSERT_EQUAL(ModemAtResult::Ok, ctx.lastResult);
  TEST_ASSERT_EQUAL_STRING("", ctx.response);
}

void test_response_line_is_captured_before_ok() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT+CEREG?", 1000, captureAtResult, &ctx));
  core.poll(10, captureWrite, &ctx);
  feedLine(core, "+CEREG: 0,1");
  feedLine(core, "OK");

  TEST_ASSERT_EQUAL(ModemAtResult::Ok, ctx.lastResult);
  TEST_ASSERT_EQUAL_STRING("+CEREG: 0,1\n", ctx.response);
}

void test_error_finishes_command() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT+BAD", 1000, captureAtResult, &ctx));
  core.poll(10, captureWrite, &ctx);
  feedLine(core, "ERROR");

  TEST_ASSERT_EQUAL(1, ctx.callbackCount);
  TEST_ASSERT_EQUAL(ModemAtResult::Error, ctx.lastResult);
  TEST_ASSERT_EQUAL_STRING("ERROR\n", ctx.response);
}

void test_cme_error_finishes_command() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT+BAD", 1000, captureAtResult, &ctx));
  core.poll(10, captureWrite, &ctx);
  feedLine(core, "+CME ERROR: 10");

  TEST_ASSERT_EQUAL(1, ctx.callbackCount);
  TEST_ASSERT_EQUAL(ModemAtResult::Error, ctx.lastResult);
  TEST_ASSERT_EQUAL_STRING("+CME ERROR: 10\n", ctx.response);
}

void test_cmt_urc_does_not_pollute_running_command_response() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);
  core.setUrcCallback(captureUrc, &ctx);

  TEST_ASSERT_TRUE(core.submit("AT", 1000, captureAtResult, &ctx));
  core.poll(10, captureWrite, &ctx);
  feedLine(core, "+CMT: ,24");
  feedLine(core, "0891683108200505F0");
  feedLine(core, "OK");

  TEST_ASSERT_EQUAL(2, ctx.urcCount);
  TEST_ASSERT_EQUAL_STRING("+CMT: ,24", ctx.urcs[0]);
  TEST_ASSERT_EQUAL_STRING("0891683108200505F0", ctx.urcs[1]);
  TEST_ASSERT_EQUAL(ModemAtResult::Ok, ctx.lastResult);
  TEST_ASSERT_EQUAL_STRING("", ctx.response);
}

void test_cmt_pdu_line_longer_than_legacy_buffer_is_emitted_intact() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);
  core.setUrcCallback(captureUrc, &ctx);

  char longPdu[601];
  for (size_t i = 0; i < sizeof(longPdu) - 1; ++i) {
    longPdu[i] = (i % 2 == 0) ? 'A' : '1';
  }
  longPdu[sizeof(longPdu) - 1] = '\0';

  feedLine(core, "+CMT: ,300");
  feedLine(core, longPdu);

  TEST_ASSERT_EQUAL(2, ctx.urcCount);
  TEST_ASSERT_EQUAL_STRING("+CMT: ,300", ctx.urcs[0]);
  TEST_ASSERT_EQUAL_STRING(longPdu, ctx.urcs[1]);
}

void test_over_capacity_line_is_discarded_until_newline_and_parser_recovers() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);
  core.setUrcCallback(captureUrc, &ctx);

  char tooLongLine[ModemAtCore::kLineCapacity + 32];
  for (size_t i = 0; i < sizeof(tooLongLine) - 1; ++i) {
    tooLongLine[i] = (i % 2 == 0) ? 'B' : '2';
  }
  tooLongLine[sizeof(tooLongLine) - 1] = '\0';

  feedLine(core, "+CMT: ,999");
  feedLine(core, tooLongLine);
  feedLine(core, "+CEREG: 0,1");

  TEST_ASSERT_EQUAL(2, ctx.urcCount);
  TEST_ASSERT_EQUAL_STRING("+CMT: ,999", ctx.urcs[0]);
  TEST_ASSERT_EQUAL_STRING("+CEREG: 0,1", ctx.urcs[1]);
}

void test_timeout_finishes_current_and_next_command_can_start() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT+ONE", 100, captureAtResult, &ctx));
  TEST_ASSERT_TRUE(core.submit("AT+TWO", 100, captureAtResult, &ctx));
  core.poll(0, captureWrite, &ctx);
  core.poll(101, captureWrite, &ctx);

  TEST_ASSERT_EQUAL(1, ctx.callbackCount);
  TEST_ASSERT_EQUAL(ModemAtResult::Timeout, ctx.lastResult);
  TEST_ASSERT_TRUE(core.isBusy());
  TEST_ASSERT_EQUAL_STRING("AT+ONE\r\nAT+TWO\r\n", ctx.writes);
}

void test_queue_capacity_is_enforced() {
  TestContext ctx;
  resetContext(ctx);
  ModemAtCore core;
  core.begin(0);

  TEST_ASSERT_TRUE(core.submit("AT+1", 1000, captureAtResult, &ctx));
  TEST_ASSERT_TRUE(core.submit("AT+2", 1000, captureAtResult, &ctx));
  TEST_ASSERT_TRUE(core.submit("AT+3", 1000, captureAtResult, &ctx));
  TEST_ASSERT_TRUE(core.submit("AT+4", 1000, captureAtResult, &ctx));
  TEST_ASSERT_FALSE(core.submit("AT+5", 1000, captureAtResult, &ctx));
  TEST_ASSERT_EQUAL(4, core.queueDepth());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;

  UNITY_BEGIN();
  RUN_TEST(test_command_starts_and_ok_finishes);
  RUN_TEST(test_response_line_is_captured_before_ok);
  RUN_TEST(test_error_finishes_command);
  RUN_TEST(test_cme_error_finishes_command);
  RUN_TEST(test_cmt_urc_does_not_pollute_running_command_response);
  RUN_TEST(test_cmt_pdu_line_longer_than_legacy_buffer_is_emitted_intact);
  RUN_TEST(test_over_capacity_line_is_discarded_until_newline_and_parser_recovers);
  RUN_TEST(test_timeout_finishes_current_and_next_command_can_start);
  RUN_TEST(test_queue_capacity_is_enforced);
  return UNITY_END();
}
