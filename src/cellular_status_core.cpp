#include "cellular_status_core.h"

#include <stdio.h>
#include <string.h>

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
