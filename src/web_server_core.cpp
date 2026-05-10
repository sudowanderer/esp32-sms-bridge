#include "web_server_core.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

class JsonWriter {
 public:
  JsonWriter(char* output, size_t outputSize) : output_(output), outputSize_(outputSize) {
    if (output_ != nullptr && outputSize_ > 0) {
      output_[0] = '\0';
    }
  }

  bool append(const char* text) {
    if (text == nullptr) {
      text = "";
    }

    const size_t length = strlen(text);
    if (length >= remaining()) {
      markFull();
      return false;
    }

    memcpy(output_ + used_, text, length + 1);
    used_ += length;
    return true;
  }

  bool appendFormat(const char* format, ...) {
    if (remaining() == 0) {
      ok_ = false;
      return false;
    }

    va_list args;
    va_start(args, format);
    const int written = vsnprintf(output_ + used_, remaining(), format, args);
    va_end(args);

    if (written < 0 || static_cast<size_t>(written) >= remaining()) {
      markFull();
      return false;
    }

    used_ += static_cast<size_t>(written);
    return true;
  }

  bool appendEscaped(const char* text) {
    char escaped[256];
    if (!webJsonEscape(text, escaped, sizeof(escaped))) {
      ok_ = false;
      return false;
    }
    return append(escaped);
  }

  bool ok() const {
    return ok_;
  }

 private:
  size_t remaining() const {
    if (output_ == nullptr || outputSize_ == 0 || used_ >= outputSize_) {
      return 0;
    }

    return outputSize_ - used_;
  }

  void markFull() {
    ok_ = false;
    if (output_ != nullptr && outputSize_ > 0) {
      output_[outputSize_ - 1] = '\0';
      used_ = outputSize_ - 1;
    }
  }

  char* output_;
  size_t outputSize_;
  size_t used_ = 0;
  bool ok_ = true;
};

static const char* boolJson(bool value) {
  return value ? "true" : "false";
}

bool webJsonEscape(const char* input, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return false;
  }

  if (input == nullptr) {
    input = "";
  }

  size_t used = 0;
  bool ok = true;
  for (size_t i = 0; input[i] != '\0'; ++i) {
    const unsigned char ch = static_cast<unsigned char>(input[i]);
    const char* replacement = nullptr;
    char unicodeEscape[7] = "";

    switch (ch) {
      case '"':
        replacement = "\\\"";
        break;
      case '\\':
        replacement = "\\\\";
        break;
      case '\b':
        replacement = "\\b";
        break;
      case '\f':
        replacement = "\\f";
        break;
      case '\n':
        replacement = "\\n";
        break;
      case '\r':
        replacement = "\\r";
        break;
      case '\t':
        replacement = "\\t";
        break;
      default:
        if (ch < 0x20) {
          snprintf(unicodeEscape, sizeof(unicodeEscape), "\\u%04x", ch);
          replacement = unicodeEscape;
        }
        break;
    }

    if (replacement != nullptr) {
      const size_t length = strlen(replacement);
      if (used + length >= outputSize) {
        ok = false;
        break;
      }
      memcpy(output + used, replacement, length);
      used += length;
    } else {
      if (used + 1 >= outputSize) {
        ok = false;
        break;
      }
      output[used++] = static_cast<char>(ch);
    }
  }

  output[used] = '\0';
  return ok;
}

bool webBuildStatusJson(const WebStatusSnapshot& status, char* output, size_t outputSize) {
  JsonWriter writer(output, outputSize);

  writer.append("{");
  writer.appendFormat("\"uptime_ms\":%lu,", static_cast<unsigned long>(status.uptimeMs));
  writer.appendFormat("\"free_heap\":%lu,", static_cast<unsigned long>(status.freeHeap));
  writer.appendFormat("\"modem\":{\"busy\":%s,\"queue_depth\":%u},", boolJson(status.modemBusy), status.modemQueueDepth);
  writer.appendFormat("\"sms_queue\":{\"depth\":%u,\"pending\":%u},", status.smsQueueDepth, status.smsQueuePending);
  writer.appendFormat("\"wifi\":{\"configured\":%s,\"connected\":%s,\"status\":\"",
                      boolJson(status.wifiConfigured),
                      boolJson(status.wifiConnected));
  writer.appendEscaped(status.wifiStatus);
  writer.append("\",\"ip\":\"");
  writer.appendEscaped(status.wifiIp);
  writer.append("\"},");
  writer.appendFormat("\"forwarder_http\":{\"configured\":%s,\"status\":\"", boolJson(status.forwarderConfigured));
  writer.appendEscaped(status.forwarderStatus);
  writer.appendFormat("\",\"last_code\":%d,\"last_error\":\"", status.forwarderLastCode);
  writer.appendEscaped(status.forwarderLastError);
  writer.append("\"},");
  writer.appendFormat("\"logs\":{\"count\":%u}", status.loggerCount);
  writer.append("}");

  return writer.ok();
}

bool webBuildLogEntryJson(const LoggerEntry& entry, char* output, size_t outputSize) {
  JsonWriter writer(output, outputSize);

  writer.appendFormat("{\"seq\":%lu,\"time_ms\":%lu,\"level\":\"%s\",\"message\":\"",
                      static_cast<unsigned long>(entry.sequence),
                      static_cast<unsigned long>(entry.timeMs),
                      loggerLevelName(entry.level));
  writer.appendEscaped(entry.message);
  writer.append("\"}");

  return writer.ok();
}

bool webBuildQueueItemJson(const SmsQueueItem& item, uint8_t index, char* output, size_t outputSize) {
  JsonWriter writer(output, outputSize);

  writer.appendFormat("{\"index\":%u,\"sender\":\"", index);
  writer.appendEscaped(item.message.sender);
  writer.append("\",\"timestamp\":\"");
  writer.appendEscaped(item.message.timestamp);
  writer.appendFormat("\",\"status\":\"%s\",\"attempts\":%u,", webSmsQueueStatusName(item.status), item.attemptCount);
  writer.appendFormat("\"created_at_ms\":%lu,\"updated_at_ms\":%lu,\"next_attempt_ms\":%lu,\"last_error\":\"",
                      static_cast<unsigned long>(item.createdAtMs),
                      static_cast<unsigned long>(item.updatedAtMs),
                      static_cast<unsigned long>(item.nextAttemptMs));
  writer.appendEscaped(item.lastError);
  writer.append("\"}");

  return writer.ok();
}

const char* webSmsQueueStatusName(SmsQueueStatus status) {
  switch (status) {
    case SmsQueueStatus::Pending:
      return "pending";
    case SmsQueueStatus::Sending:
      return "sending";
    case SmsQueueStatus::Sent:
      return "sent";
    case SmsQueueStatus::Failed:
      return "failed";
  }

  return "unknown";
}
