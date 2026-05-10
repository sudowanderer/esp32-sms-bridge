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

static bool isWhitespace(char ch) {
  return ch == ' ' || ch == '\n' || ch == '\r' || ch == '\t';
}

static const char* skipWhitespace(const char* text) {
  while (text != nullptr && isWhitespace(*text)) {
    ++text;
  }
  return text;
}

static bool jsonHasObjectBounds(const char* json) {
  const char* begin = skipWhitespace(json);
  if (begin == nullptr || *begin != '{') {
    return false;
  }

  const char* end = begin + strlen(begin);
  while (end > begin && isWhitespace(*(end - 1))) {
    --end;
  }

  return end > begin && *(end - 1) == '}';
}

static int hexValue(char ch) {
  if (ch >= '0' && ch <= '9') {
    return ch - '0';
  }
  if (ch >= 'a' && ch <= 'f') {
    return ch - 'a' + 10;
  }
  if (ch >= 'A' && ch <= 'F') {
    return ch - 'A' + 10;
  }
  return -1;
}

static bool appendUtf8FromCodepoint(uint16_t codepoint, char* output, size_t outputSize, size_t& used) {
  if (codepoint < 0x80) {
    if (used + 1 >= outputSize) {
      return false;
    }
    output[used++] = static_cast<char>(codepoint);
    return true;
  }

  if (codepoint < 0x800) {
    if (used + 2 >= outputSize) {
      return false;
    }
    output[used++] = static_cast<char>(0xC0 | (codepoint >> 6));
    output[used++] = static_cast<char>(0x80 | (codepoint & 0x3F));
    return true;
  }

  if (used + 3 >= outputSize) {
    return false;
  }
  output[used++] = static_cast<char>(0xE0 | (codepoint >> 12));
  output[used++] = static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
  output[used++] = static_cast<char>(0x80 | (codepoint & 0x3F));
  return true;
}

static bool decodeJsonString(const char* begin, const char* end, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0 || begin == nullptr || end == nullptr || end < begin) {
    return false;
  }

  size_t used = 0;
  for (const char* cursor = begin; cursor < end; ++cursor) {
    unsigned char ch = static_cast<unsigned char>(*cursor);
    if (ch == '\\') {
      ++cursor;
      if (cursor >= end) {
        return false;
      }

      switch (*cursor) {
        case '"':
        case '\\':
        case '/':
          ch = static_cast<unsigned char>(*cursor);
          break;
        case 'b':
          ch = '\b';
          break;
        case 'f':
          ch = '\f';
          break;
        case 'n':
          ch = '\n';
          break;
        case 'r':
          ch = '\r';
          break;
        case 't':
          ch = '\t';
          break;
        case 'u': {
          if (end - cursor < 5) {
            return false;
          }
          uint16_t codepoint = 0;
          for (uint8_t i = 0; i < 4; ++i) {
            const int value = hexValue(*(cursor + 1 + i));
            if (value < 0) {
              return false;
            }
            codepoint = static_cast<uint16_t>((codepoint << 4) | value);
          }
          if (!appendUtf8FromCodepoint(codepoint, output, outputSize, used)) {
            return false;
          }
          cursor += 4;
          continue;
        }
        default:
          return false;
      }
    }

    if (used + 1 >= outputSize) {
      return false;
    }
    output[used++] = static_cast<char>(ch);
  }

  output[used] = '\0';
  return true;
}

static WebConfigParseResult extractJsonStringField(const char* json,
                                                   const char* key,
                                                   char* output,
                                                   size_t outputSize,
                                                   bool& found) {
  found = false;

  char pattern[64];
  const int patternLength = snprintf(pattern, sizeof(pattern), "\"%s\"", key);
  if (patternLength <= 0 || static_cast<size_t>(patternLength) >= sizeof(pattern)) {
    return WebConfigParseResult::InvalidJson;
  }

  const char* cursor = strstr(json, pattern);
  if (cursor == nullptr) {
    return WebConfigParseResult::Ok;
  }

  cursor += patternLength;
  cursor = skipWhitespace(cursor);
  if (cursor == nullptr || *cursor != ':') {
    return WebConfigParseResult::InvalidJson;
  }

  ++cursor;
  cursor = skipWhitespace(cursor);
  if (cursor == nullptr || *cursor != '"') {
    return WebConfigParseResult::InvalidJson;
  }

  ++cursor;
  const char* valueBegin = cursor;
  bool escaped = false;
  while (*cursor != '\0') {
    if (escaped) {
      escaped = false;
    } else if (*cursor == '\\') {
      escaped = true;
    } else if (*cursor == '"') {
      break;
    }
    ++cursor;
  }

  if (*cursor != '"') {
    return WebConfigParseResult::InvalidJson;
  }

  char decoded[128];
  if (outputSize > sizeof(decoded)) {
    return WebConfigParseResult::InvalidJson;
  }
  if (!decodeJsonString(valueBegin, cursor, decoded, outputSize)) {
    return WebConfigParseResult::ValueTooLong;
  }

  ConfigStoreCore::copyString(output, outputSize, decoded);
  found = true;
  return WebConfigParseResult::Ok;
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

bool webBuildConfigJson(const DeviceConfig& config, char* output, size_t outputSize) {
  JsonWriter writer(output, outputSize);

  writer.append("{");
  writer.appendFormat("\"version\":%u,", config.version);
  writer.appendFormat("\"wifi\":{\"configured\":%s,\"ssid\":\"", boolJson(ConfigStoreCore::hasWifiConfig(config)));
  writer.appendEscaped(config.wifiSsid);
  writer.appendFormat("\",\"password_set\":%s},", boolJson(config.wifiPassword[0] != '\0'));
  writer.appendFormat("\"bark\":{\"configured\":%s,\"server_url\":\"", boolJson(ConfigStoreCore::hasBarkConfig(config)));
  writer.appendEscaped(config.barkServerUrl);
  writer.appendFormat("\",\"device_key_set\":%s}", boolJson(config.barkDeviceKey[0] != '\0'));
  writer.append("}");

  return writer.ok();
}

WebConfigParseResult webParseConfigSaveJson(const char* json, DeviceConfig& config) {
  if (json == nullptr || !jsonHasObjectBounds(json)) {
    return WebConfigParseResult::InvalidJson;
  }

  DeviceConfig next = config;
  bool found = false;
  WebConfigParseResult result = extractJsonStringField(json, "wifi_ssid", next.wifiSsid, sizeof(next.wifiSsid), found);
  if (result != WebConfigParseResult::Ok) {
    return result;
  }

  result = extractJsonStringField(json, "wifi_password", next.wifiPassword, sizeof(next.wifiPassword), found);
  if (result != WebConfigParseResult::Ok) {
    return result;
  }

  result = extractJsonStringField(json, "bark_server_url", next.barkServerUrl, sizeof(next.barkServerUrl), found);
  if (result != WebConfigParseResult::Ok) {
    return result;
  }

  result = extractJsonStringField(json, "bark_device_key", next.barkDeviceKey, sizeof(next.barkDeviceKey), found);
  if (result != WebConfigParseResult::Ok) {
    return result;
  }

  ConfigStoreCore::sanitize(next);
  config = next;
  return WebConfigParseResult::Ok;
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
