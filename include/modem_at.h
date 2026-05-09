#pragma once

#include <stdint.h>

enum class ModemAtResult {
  Ok,
  Error,
  Timeout,
  QueueFull,
};

// AT 命令完成时调用。response 只在回调函数执行期间保证有效，调用方不要长期保存指针。
using ModemAtCallback = void (*)(ModemAtResult result, const char* response, void* userData);

// URC 是模块主动上报的数据，例如 +CMT 短信通知；它不是某个 AT 命令的直接返回值。
using ModemUrcCallback = void (*)(const char* line, void* userData);

void modemAtBegin();

// 主循环必须频繁调用 poll()，它负责读取 Serial1、推进命令队列和处理超时。
void modemAtPoll();

// 提交 AT 命令只会入队，不会等待模块返回；结果通过 callback 异步通知。
bool modemAtSubmit(const char* command, uint32_t timeoutMs, ModemAtCallback callback, void* userData);
void modemAtSetUrcCallback(ModemUrcCallback callback, void* userData);

bool modemAtIsBusy();
uint8_t modemAtQueueDepth();
