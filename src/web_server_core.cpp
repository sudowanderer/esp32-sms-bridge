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
  writer.appendFormat("\"cellular\":{\"signal_known\":%s,\"csq\":%u,\"rssi_dbm\":%d,\"registration_known\":%s,\"registration_status\":%u,\"registration\":\"",
                      boolJson(status.cellularSignalKnown),
                      status.cellularCsq,
                      static_cast<int>(status.cellularRssiDbm),
                      boolJson(status.cellularRegistrationKnown),
                      status.cellularRegistrationStatus);
  writer.appendEscaped(status.cellularRegistrationText);
  writer.append("\"},");
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

static const char* pageTitle(WebPageKind page) {
  switch (page) {
    case WebPageKind::Status:
      return "Status";
    case WebPageKind::Config:
      return "Configuration";
    case WebPageKind::Queue:
      return "Queue";
    case WebPageKind::Logs:
      return "Logs";
  }
  return "Status";
}

static const char* activeClass(WebPageKind current, WebPageKind item) {
  return current == item ? " class=\"active\"" : "";
}

static bool appendPageScript(JsonWriter& writer, WebPageKind page) {
  switch (page) {
    case WebPageKind::Status:
      writer.append(
          "<script>"
          "const $=id=>document.getElementById(id);"
          "function fmtMs(ms){const s=Math.floor(ms/1000),d=Math.floor(s/86400),h=Math.floor(s/3600)%24,m=Math.floor(s/60)%60;return (d?d+'d ':'')+h+'h '+m+'m';}"
          "async function load(){const r=await fetch('/api/status');const j=await r.json();"
          "$('uptime').textContent=fmtMs(j.uptime_ms);$('heap').textContent=j.free_heap;"
          "$('wifi').textContent=j.wifi.status+(j.wifi.ip?' / '+j.wifi.ip:'');"
          "$('cell').textContent=(j.cellular.registration||'unknown')+' / '+(j.cellular.signal_known?j.cellular.rssi_dbm+' dBm (CSQ '+j.cellular.csq+')':'signal unknown');"
          "$('modem').textContent=(j.modem.busy?'busy':'idle')+' / queue '+j.modem.queue_depth;"
          "$('sms').textContent='depth '+j.sms_queue.depth+' / pending '+j.sms_queue.pending;"
          "$('push').textContent=j.forwarder_http.status+' / code '+j.forwarder_http.last_code;"
          "$('logs').textContent=j.logs.count;}"
          "load();setInterval(load,5000);"
          "</script>");
      return writer.ok();
    case WebPageKind::Config:
      writer.append(
          "<script>"
          "const $=id=>document.getElementById(id);"
          "async function load(){const r=await fetch('/api/config');const j=await r.json();$('wifi_ssid').value=j.wifi.ssid;$('bark_server_url').value=j.bark.server_url;$('wifi_set').textContent=j.wifi.password_set?'set':'not set';$('bark_set').textContent=j.bark.device_key_set?'set':'not set';}"
          "async function save(e){e.preventDefault();const body={wifi_ssid:$('wifi_ssid').value,bark_server_url:$('bark_server_url').value};if($('wifi_password').value)body.wifi_password=$('wifi_password').value;if($('bark_device_key').value)body.bark_device_key=$('bark_device_key').value;const r=await fetch('/api/config/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)});$('result').textContent=r.ok?'saved':'save failed';if(r.ok){$('wifi_password').value='';$('bark_device_key').value='';load();}}"
          "document.getElementById('cfg').addEventListener('submit',save);load();"
          "</script>");
      return writer.ok();
    case WebPageKind::Queue:
      writer.append(
          "<script>"
          "function h(v){return String(v==null?'':v).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}"
          "async function load(){const r=await fetch('/api/queue');const j=await r.json();document.getElementById('body').innerHTML=j.queue.map(x=>'<tr><td>'+h(x.index)+'</td><td>'+h(x.sender)+'</td><td>'+h(x.timestamp)+'</td><td>'+h(x.status)+'</td><td>'+h(x.attempts)+'</td><td>'+h(x.last_error)+'</td></tr>').join('');}"
          "load();setInterval(load,5000);"
          "</script>");
      return writer.ok();
    case WebPageKind::Logs:
      writer.append(
          "<script>"
          "function h(v){return String(v==null?'':v).replace(/[&<>\"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','\"':'&quot;',\"'\":'&#39;'}[c]));}"
          "async function load(){const r=await fetch('/api/logs?limit=80');const j=await r.json();document.getElementById('body').innerHTML=j.logs.map(x=>'<tr><td>'+h(x.seq)+'</td><td>'+h(x.time_ms)+'</td><td>'+h(x.level)+'</td><td>'+h(x.message)+'</td></tr>').join('');}"
          "load();setInterval(load,5000);"
          "</script>");
      return writer.ok();
  }
  return false;
}

bool webBuildPageHtml(WebPageKind page, char* output, size_t outputSize) {
  JsonWriter writer(output, outputSize);
  const char* title = pageTitle(page);

  writer.append("<!doctype html><html><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  writer.appendFormat("<title>SMS Bridge - %s</title>", title);
  writer.append(
      "<style>"
      "body{margin:0;font:14px/1.45 -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#f3f5f7;color:#1f2933}"
      "header{background:#102a43;color:#fff;padding:10px 16px;border-bottom:3px solid #2f80ed}"
      "header h1{font-size:18px;margin:0}"
      "nav{background:#243b53;padding:0 12px}"
      "nav a{display:inline-block;color:#d9e2ec;text-decoration:none;padding:9px 12px}"
      "nav a.active{background:#f3f5f7;color:#102a43}"
      "main{max-width:980px;margin:18px auto;padding:0 12px}"
      "section{background:#fff;border:1px solid #d9e2ec;margin-bottom:14px}"
      "h2{font-size:16px;margin:0;padding:9px 12px;background:#eef2f6;border-bottom:1px solid #d9e2ec}"
      "table{width:100%;border-collapse:collapse}"
      "td,th{padding:8px 10px;border-bottom:1px solid #edf1f5;text-align:left;vertical-align:top}"
      "th{background:#f8fafc;font-weight:600}"
      "td:first-child{width:190px;color:#52606d}"
      "label{display:block;margin:10px 0 4px;color:#52606d}"
      "input{width:100%;box-sizing:border-box;padding:8px;border:1px solid #bcccdc;border-radius:3px;background:#fff}"
      "button{margin-top:12px;padding:8px 14px;border:1px solid #1f6feb;background:#1f6feb;color:#fff;border-radius:3px}"
      ".hint{color:#697b8c;font-size:12px}"
      "</style></head><body><header><h1>SMS Bridge</h1></header><nav>");
  writer.appendFormat("<a href=\"/\"%s>Status</a>", activeClass(page, WebPageKind::Status));
  writer.appendFormat("<a href=\"/config\"%s>Config</a>", activeClass(page, WebPageKind::Config));
  writer.appendFormat("<a href=\"/queue\"%s>Queue</a>", activeClass(page, WebPageKind::Queue));
  writer.appendFormat("<a href=\"/logs\"%s>Logs</a>", activeClass(page, WebPageKind::Logs));
  writer.append("</nav><main>");

  switch (page) {
    case WebPageKind::Status:
      writer.append(
          "<section><h2>System</h2><table>"
          "<tr><td>Uptime</td><td id=\"uptime\">loading</td></tr>"
          "<tr><td>Free heap</td><td id=\"heap\">loading</td></tr>"
          "<tr><td>Logs</td><td id=\"logs\">loading</td></tr>"
          "</table></section>"
          "<section><h2>Network</h2><table>"
          "<tr><td>WiFi</td><td id=\"wifi\">loading</td></tr>"
          "<tr><td>4G Modem</td><td id=\"cell\">loading</td></tr>"
          "</table></section>"
          "<section><h2>Services</h2><table>"
          "<tr><td>Modem AT</td><td id=\"modem\">loading</td></tr>"
          "<tr><td>SMS Queue</td><td id=\"sms\">loading</td></tr>"
          "<tr><td>HTTP Forwarder</td><td id=\"push\">loading</td></tr>"
          "</table></section>");
      break;
    case WebPageKind::Config:
      writer.append(
          "<section><h2>Configuration</h2><form id=\"cfg\">"
          "<label for=\"wifi_ssid\">WiFi SSID</label><input id=\"wifi_ssid\" name=\"wifi_ssid\">"
          "<label for=\"wifi_password\">WiFi password <span class=\"hint\">current: <span id=\"wifi_set\">loading</span>, leave blank to keep</span></label><input id=\"wifi_password\" name=\"wifi_password\" type=\"password\">"
          "<label for=\"bark_server_url\">Bark server URL</label><input id=\"bark_server_url\" name=\"bark_server_url\">"
          "<label for=\"bark_device_key\">Bark device key <span class=\"hint\">current: <span id=\"bark_set\">loading</span>, leave blank to keep</span></label><input id=\"bark_device_key\" name=\"bark_device_key\" type=\"password\">"
          "<button type=\"submit\">Save</button> <span id=\"result\" class=\"hint\"></span>"
          "</form></section>");
      break;
    case WebPageKind::Queue:
      writer.append(
          "<section><h2>Queue</h2><table><thead><tr><th>#</th><th>Sender</th><th>Timestamp</th><th>Status</th><th>Attempts</th><th>Last error</th></tr></thead><tbody id=\"body\"></tbody></table></section>");
      break;
    case WebPageKind::Logs:
      writer.append(
          "<section><h2>Logs</h2><table><thead><tr><th>Seq</th><th>Time ms</th><th>Level</th><th>Message</th></tr></thead><tbody id=\"body\"></tbody></table></section>");
      break;
  }

  appendPageScript(writer, page);
  writer.append("</main></body></html>");
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
