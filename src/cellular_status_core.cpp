#include "cellular_status_core.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const char* findLine(const char* response, const char* prefix) {
  if (response == nullptr || prefix == nullptr) {
    return nullptr;
  }

  const char* cursor = response;
  const size_t prefixLength = strlen(prefix);
  while (*cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    if (strncmp(cursor, prefix, prefixLength) == 0) {
      return cursor;
    }
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }

  return nullptr;
}

static bool isLineTerminator(char ch) {
  return ch == '\r' || ch == '\n' || ch == '\0';
}

static const char* trimLeft(const char* text) {
  while (text != nullptr && (*text == ' ' || *text == '\t')) {
    ++text;
  }
  return text;
}

static void copyTrimmedLine(const char* line, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return;
  }

  output[0] = '\0';
  if (line == nullptr) {
    return;
  }

  const char* begin = trimLeft(line);
  const char* end = begin;
  while (!isLineTerminator(*end)) {
    ++end;
  }
  while (end > begin && (*(end - 1) == ' ' || *(end - 1) == '\t')) {
    --end;
  }

  size_t length = static_cast<size_t>(end - begin);
  if (length >= outputSize) {
    length = outputSize - 1;
  }
  memcpy(output, begin, length);
  output[length] = '\0';
}

static bool isUsefulResponseLine(const char* line) {
  char value[16];
  copyTrimmedLine(line, value, sizeof(value));
  return value[0] != '\0' && strcmp(value, "OK") != 0 && strcmp(value, "ERROR") != 0 && strncmp(value, "AT", 2) != 0;
}

static bool copyNextUsefulLine(const char*& cursor, char* output, size_t outputSize) {
  while (cursor != nullptr && *cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    const char* line = cursor;
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
    if (isUsefulResponseLine(line)) {
      copyTrimmedLine(line, output, outputSize);
      return true;
    }
  }
  return false;
}

static bool copyDigitsLine(const char* response, char* output, size_t outputSize) {
  const char* cursor = response;
  while (cursor != nullptr && *cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    const char* line = trimLeft(cursor);
    bool hasDigit = false;
    const char* end = line;
    while (!isLineTerminator(*end)) {
      if (*end >= '0' && *end <= '9') {
        hasDigit = true;
      } else if (*end != ' ' && *end != '\t') {
        hasDigit = false;
        break;
      }
      ++end;
    }
    if (hasDigit) {
      copyTrimmedLine(line, output, outputSize);
      return true;
    }
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }
  return false;
}

static bool extractQuotedField(const char* line, uint8_t fieldIndex, char* output, size_t outputSize) {
  if (line == nullptr || output == nullptr || outputSize == 0) {
    return false;
  }
  output[0] = '\0';

  uint8_t current = 0;
  const char* cursor = line;
  while (*cursor != '\0') {
    if (*cursor == '"') {
      ++cursor;
      const char* begin = cursor;
      while (*cursor != '\0' && *cursor != '"') {
        ++cursor;
      }
      if (*cursor != '"') {
        return false;
      }
      if (current == fieldIndex) {
        size_t length = static_cast<size_t>(cursor - begin);
        if (length >= outputSize) {
          length = outputSize - 1;
        }
        memcpy(output, begin, length);
        output[length] = '\0';
        return true;
      }
      ++current;
    }
    ++cursor;
  }
  return false;
}

static void copyIccidValue(const char* line, char* output, size_t outputSize) {
  const char* value = line;
  const char* prefix = strstr(line, "+ICCID:");
  if (prefix != nullptr) {
    value = prefix + strlen("+ICCID:");
  }
  copyTrimmedLine(value, output, outputSize);
}

static bool copyIccidLine(const char* response, char* output, size_t outputSize) {
  const char* cursor = response;
  while (cursor != nullptr && *cursor != '\0') {
    while (*cursor == '\r' || *cursor == '\n') {
      ++cursor;
    }
    char value[32];
    copyTrimmedLine(cursor, value, sizeof(value));
    bool valid = value[0] != '\0' && strncmp(value, "AT", 2) != 0 && strcmp(value, "OK") != 0 && strcmp(value, "ERROR") != 0;
    for (size_t i = 0; valid && value[i] != '\0'; ++i) {
      valid = (value[i] >= '0' && value[i] <= '9') || (value[i] >= 'A' && value[i] <= 'F') || (value[i] >= 'a' && value[i] <= 'f');
    }
    if (valid) {
      snprintf(output, outputSize, "%s", value);
      return true;
    }
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }
  return false;
}

static void copyRegistrationText(CellularStatusSnapshot& status) {
  snprintf(status.registrationText, sizeof(status.registrationText), "%s",
           CellularStatusCore::registrationStatusName(status.registrationStatus));
}

int16_t CellularStatusCore::csqToRssiDbm(uint8_t rssi) {
  if (rssi > 31) {
    return 0;
  }
  return static_cast<int16_t>(-113 + (2 * rssi));
}

const char* CellularStatusCore::registrationStatusName(uint8_t status) {
  switch (status) {
    case 0:
      return "not_registered";
    case 1:
      return "registered_home";
    case 2:
      return "searching";
    case 3:
      return "registration_denied";
    case 4:
      return "unknown";
    case 5:
      return "registered_roaming";
    default:
      return "unknown";
  }
}

const char* CellularStatusCore::rsrpQualityName(int16_t rsrpDbm, bool known) {
  if (!known) {
    return "unknown";
  }
  if (rsrpDbm >= -80) {
    return "excellent";
  }
  if (rsrpDbm >= -90) {
    return "good";
  }
  if (rsrpDbm >= -100) {
    return "fair";
  }
  return "weak";
}

bool CellularStatusCore::parseCsqResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+CSQ:");
  if (line == nullptr) {
    return false;
  }

  unsigned int rssi = 0;
  unsigned int ber = 0;
  if (sscanf(line, "+CSQ: %u,%u", &rssi, &ber) != 2 || rssi > 99 || ber > 99) {
    return false;
  }

  status.csqRssi = static_cast<uint8_t>(rssi);
  status.csqBer = static_cast<uint8_t>(ber);
  status.signalKnown = rssi <= 31;
  status.rssiDbm = status.signalKnown ? csqToRssiDbm(status.csqRssi) : 0;
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseAtiResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* cursor = response;
  if (!copyNextUsefulLine(cursor, status.manufacturer, sizeof(status.manufacturer)) ||
      !copyNextUsefulLine(cursor, status.model, sizeof(status.model)) ||
      !copyNextUsefulLine(cursor, status.firmware, sizeof(status.firmware))) {
    return false;
  }

  status.moduleInfoKnown = true;
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCesqResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+CESQ:");
  if (line == nullptr) {
    return false;
  }

  unsigned int rxlev = 0;
  unsigned int ber = 0;
  unsigned int rscp = 0;
  unsigned int ecno = 0;
  unsigned int rsrq = 0;
  unsigned int rsrp = 0;
  if (sscanf(line, "+CESQ: %u,%u,%u,%u,%u,%u", &rxlev, &ber, &rscp, &ecno, &rsrq, &rsrp) != 6 ||
      rxlev > 255 || ber > 255 || rscp > 255 || ecno > 255 || rsrq > 255 || rsrp > 255) {
    return false;
  }

  status.cesqRxlev = static_cast<uint8_t>(rxlev);
  status.cesqBer = static_cast<uint8_t>(ber);
  status.cesqRscp = static_cast<uint8_t>(rscp);
  status.cesqEcno = static_cast<uint8_t>(ecno);
  status.cesqRsrq = static_cast<uint8_t>(rsrq);
  status.cesqRsrp = static_cast<uint8_t>(rsrp);
  snprintf(status.cesqRaw,
           sizeof(status.cesqRaw),
           "%u,%u,%u,%u,%u,%u",
           rxlev,
           ber,
           rscp,
           ecno,
           rsrq,
           rsrp);
  status.lteSignalKnown = rsrp <= 97 && rsrq <= 34;
  status.rsrpDbm = status.lteSignalKnown ? static_cast<int16_t>(-140 + static_cast<int>(rsrp)) : 0;
  status.rsrqDbTenths = status.lteSignalKnown ? static_cast<int16_t>(-195 + (5 * static_cast<int>(rsrq))) : 0;
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseImsiResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  if (!copyDigitsLine(response, status.imsi, sizeof(status.imsi))) {
    return false;
  }
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseIccidResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+ICCID:");
  if (line != nullptr) {
    copyIccidValue(line, status.iccid, sizeof(status.iccid));
    status.lastUpdatedMs = nowMs;
    return status.iccid[0] != '\0';
  }

  if (!copyIccidLine(response, status.iccid, sizeof(status.iccid))) {
    return false;
  }
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCnumResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+CNUM:");
  if (line == nullptr || !extractQuotedField(line, 1, status.ownNumber, sizeof(status.ownNumber)) || status.ownNumber[0] == '\0') {
    snprintf(status.ownNumber, sizeof(status.ownNumber), "%s", "not stored or unsupported");
  }
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCopsResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+COPS:");
  if (line == nullptr || !extractQuotedField(line, 0, status.operatorName, sizeof(status.operatorName))) {
    return false;
  }
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCgactResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* cursor = response;
  bool found = false;
  bool active = false;
  while ((cursor = findLine(cursor, "+CGACT:")) != nullptr) {
    unsigned int cid = 0;
    unsigned int state = 0;
    if (sscanf(cursor, "+CGACT: %u,%u", &cid, &state) == 2) {
      found = true;
      if (state == 1) {
        active = true;
      }
    }
    while (*cursor != '\0' && *cursor != '\n') {
      ++cursor;
    }
  }

  if (!found) {
    return false;
  }
  status.dataConnectionKnown = true;
  status.dataConnectionActive = active;
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCgdccontResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+CGDCONT:");
  if (line == nullptr || !extractQuotedField(line, 1, status.apn, sizeof(status.apn))) {
    return false;
  }
  status.lastUpdatedMs = nowMs;
  return true;
}

bool CellularStatusCore::parseCeregResponse(const char* response, CellularStatusSnapshot& status, uint32_t nowMs) {
  const char* line = findLine(response, "+CEREG:");
  if (line == nullptr) {
    return false;
  }

  unsigned int n = 0;
  unsigned int stat = 0;
  if (sscanf(line, "+CEREG: %u,%u", &n, &stat) != 2 || stat > 255) {
    return false;
  }

  status.registrationKnown = true;
  status.registrationStatus = static_cast<uint8_t>(stat);
  copyRegistrationText(status);
  status.lastUpdatedMs = nowMs;
  return true;
}
