#include "modem_at_core.h"

#include "modem_commands.h"

#include <string.h>

void ModemAtCore::begin(uint32_t nowMs) {
  queueHead_ = 0;
  queueCount_ = 0;
  hasCurrent_ = false;
  currentStartedMs_ = nowMs;
  currentDeadlineMs_ = nowMs;
  lineLen_ = 0;
  lineOverflowed_ = false;
  responseLen_ = 0;
  response_[0] = '\0';
  awaitingCmtPdu_ = false;
}

void ModemAtCore::poll(uint32_t nowMs, ModemAtWriteFn writeFn, void* writeUserData) {
  // 嵌入式主循环不能长时间卡住等 OK，所以这里用时间点判断是否超时。
  if (hasCurrent_ && static_cast<int32_t>(nowMs - currentDeadlineMs_) >= 0) {
    finishCurrent(ModemAtResult::Timeout);
  }

  if (!hasCurrent_) {
    startNextCommand(nowMs, writeFn, writeUserData);
  }
}

void ModemAtCore::onByte(char c) {
  if (c == '\r') {
    return;
  }

  if (c == '\n') {
    if (lineOverflowed_) {
      lineLen_ = 0;
      lineOverflowed_ = false;
      return;
    }

    line_[lineLen_] = '\0';
    handleLine(line_);
    lineLen_ = 0;
    return;
  }

  if (lineLen_ + 1 < kLineCapacity) {
    line_[lineLen_++] = c;
    return;
  }

  // 超长行必须整行丢弃，避免把后半截 PDU 当成新的 URC/PDU 交给上层。
  lineLen_ = 0;
  lineOverflowed_ = true;
}

bool ModemAtCore::submit(const char* command, uint32_t timeoutMs, ModemAtCallback callback, void* userData) {
  if (command == nullptr || command[0] == '\0' || queueCount_ >= kQueueCapacity) {
    return false;
  }

  const uint8_t index = static_cast<uint8_t>((queueHead_ + queueCount_) % kQueueCapacity);
  strncpy(queue_[index].text, command, kCommandCapacity - 1);
  queue_[index].text[kCommandCapacity - 1] = '\0';
  queue_[index].timeoutMs = timeoutMs;
  queue_[index].callback = callback;
  queue_[index].userData = userData;
  queueCount_++;
  return true;
}

void ModemAtCore::setUrcCallback(ModemUrcCallback callback, void* userData) {
  urcCallback_ = callback;
  urcUserData_ = userData;
}

bool ModemAtCore::isBusy() const {
  return hasCurrent_;
}

uint8_t ModemAtCore::queueDepth() const {
  return queueCount_;
}

void ModemAtCore::startNextCommand(uint32_t nowMs, ModemAtWriteFn writeFn, void* writeUserData) {
  if (queueCount_ == 0 || writeFn == nullptr) {
    return;
  }

  current_ = queue_[queueHead_];
  queueHead_ = static_cast<uint8_t>((queueHead_ + 1) % kQueueCapacity);
  queueCount_--;

  hasCurrent_ = true;
  currentStartedMs_ = nowMs;
  currentDeadlineMs_ = nowMs + current_.timeoutMs;
  responseLen_ = 0;
  response_[0] = '\0';

  // AT 模块通常以 CRLF 作为命令结束符；写入由外层适配到 Serial1。
  writeFn(current_.text, writeUserData);
  writeFn("\r\n", writeUserData);
}

void ModemAtCore::finishCurrent(ModemAtResult result) {
  const ModemAtCallback callback = current_.callback;
  void* const userData = current_.userData;
  hasCurrent_ = false;

  if (callback != nullptr) {
    callback(result, response_, userData);
  }
}

void ModemAtCore::handleLine(const char* line) {
  if (line == nullptr || line[0] == '\0') {
    return;
  }

  if (awaitingCmtPdu_) {
    // PDU 模式下 +CMT header 后面紧跟一行十六进制短信数据；这行也必须按 URC 交出去。
    awaitingCmtPdu_ = false;
    emitUrc(line);
    return;
  }

  if (strcmp(line, "OK") == 0) {
    if (hasCurrent_) {
      finishCurrent(ModemAtResult::Ok);
    } else {
      emitUrc(line);
    }
    return;
  }

  if (strcmp(line, "ERROR") == 0 || isTerminalError(line)) {
    if (hasCurrent_) {
      appendResponseLine(line);
      finishCurrent(ModemAtResult::Error);
    } else {
      emitUrc(line);
    }
    return;
  }

  if (isUrcLine(line) && !currentCommandExpectsPlusLine(line)) {
    // URC 可能插入在 AT 响应中间，不能追加到当前命令响应里，否则短信会被业务命令吞掉。
    emitUrc(line);
    if (startsWith(line, "+CMT:")) {
      awaitingCmtPdu_ = true;
    }
    return;
  }

  if (hasCurrent_) {
    appendResponseLine(line);
    return;
  }

  emitUrc(line);
}

bool ModemAtCore::isTerminalError(const char* line) const {
  return startsWith(line, "+CME ERROR:") || startsWith(line, "+CMS ERROR:");
}

bool ModemAtCore::isUrcLine(const char* line) const {
  if (startsWith(line, "+CMT:") || startsWith(line, "+CMTI:") || startsWith(line, "+CDS:")) {
    return true;
  }

  if (startsWith(line, "+CEREG:")) {
    return true;
  }

  if (strcmp(line, "+MATREADY") == 0 || startsWith(line, "+CPIN:")) {
    return true;
  }

  return false;
}

bool ModemAtCore::currentCommandExpectsPlusLine(const char* line) const {
  if (!hasCurrent_) {
    return false;
  }

  // 同一个前缀既可能是主动 URC，也可能是查询命令响应；这里先处理 v0 会主动查询的 CEREG。
  if (startsWith(line, "+CEREG:") && startsWith(current_.text, ModemCommands::queryRegistration())) {
    return true;
  }

  return false;
}

void ModemAtCore::appendResponseLine(const char* line) {
  const size_t lineLen = strlen(line);
  const size_t needed = lineLen + 1;

  if (responseLen_ + needed + 1 >= kResponseCapacity) {
    return;
  }

  memcpy(response_ + responseLen_, line, lineLen);
  responseLen_ += lineLen;
  response_[responseLen_++] = '\n';
  response_[responseLen_] = '\0';
}

void ModemAtCore::emitUrc(const char* line) {
  if (urcCallback_ != nullptr) {
    urcCallback_(line, urcUserData_);
  }
}

bool ModemAtCore::startsWith(const char* value, const char* prefix) {
  return strncmp(value, prefix, strlen(prefix)) == 0;
}
