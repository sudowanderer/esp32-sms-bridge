#pragma once

#include "modem_at.h"

#include <stddef.h>
#include <stdint.h>

using ModemAtWriteFn = void (*)(const char* data, void* userData);

class ModemAtCore {
 public:
  static constexpr uint8_t kQueueCapacity = 4;
  static constexpr size_t kCommandCapacity = 96;
  static constexpr size_t kLineCapacity = 1536;
  static constexpr size_t kResponseCapacity = 1024;

  void begin(uint32_t nowMs);

  // 纯状态机核心不直接依赖 Arduino Serial1；外层通过 writeFn 把待发送数据写到真实串口。
  void poll(uint32_t nowMs, ModemAtWriteFn writeFn, void* writeUserData);
  void onByte(char c);

  bool submit(const char* command, uint32_t timeoutMs, ModemAtCallback callback, void* userData);
  void setUrcCallback(ModemUrcCallback callback, void* userData);

  bool isBusy() const;
  uint8_t queueDepth() const;

 private:
  struct Command {
    char text[kCommandCapacity];
    uint32_t timeoutMs = 0;
    ModemAtCallback callback = nullptr;
    void* userData = nullptr;
  };

  // 队列只存等待发送的命令；正在执行的命令单独放在 current_。
  void startNextCommand(uint32_t nowMs, ModemAtWriteFn writeFn, void* writeUserData);
  void finishCurrent(ModemAtResult result);
  void handleLine(const char* line);
  bool isTerminalError(const char* line) const;
  bool isUrcLine(const char* line) const;
  bool currentCommandExpectsPlusLine(const char* line) const;
  void appendResponseLine(const char* line);
  void emitUrc(const char* line);
  static bool startsWith(const char* value, const char* prefix);

  Command queue_[kQueueCapacity];
  uint8_t queueHead_ = 0;
  uint8_t queueCount_ = 0;

  Command current_;
  bool hasCurrent_ = false;
  uint32_t currentStartedMs_ = 0;
  uint32_t currentDeadlineMs_ = 0;

  char line_[kLineCapacity];
  size_t lineLen_ = 0;
  bool lineOverflowed_ = false;

  char response_[kResponseCapacity];
  size_t responseLen_ = 0;

  bool awaitingCmtPdu_ = false;

  ModemUrcCallback urcCallback_ = nullptr;
  void* urcUserData_ = nullptr;
};
