#include "modem_commands.h"

#include <stdio.h>

namespace {

bool buildIndexedCommand(const char* prefix, uint16_t index, char* output, size_t outputSize) {
  if (output == nullptr || outputSize == 0) {
    return false;
  }

  output[0] = '\0';
  const int written = snprintf(output, outputSize, "%s%u", prefix, static_cast<unsigned>(index));
  if (written < 0 || static_cast<size_t>(written) >= outputSize) {
    output[0] = '\0';
    return false;
  }

  return true;
}

}  // namespace

namespace ModemCommands {

const char* attention() {
  return "AT";
}

const char* smsPduMode() {
  return "AT+CMGF=0";
}

const char* smsDirectUrcMode() {
  return "AT+CNMI=2,2,0,0,0";
}

const char* queryRegistration() {
  return "AT+CEREG?";
}

const char* querySignal() {
  return "AT+CSQ";
}

bool buildReadStoredSms(uint16_t index, char* output, size_t outputSize) {
  return buildIndexedCommand("AT+CMGR=", index, output, outputSize);
}

bool buildDeleteStoredSms(uint16_t index, char* output, size_t outputSize) {
  return buildIndexedCommand("AT+CMGD=", index, output, outputSize);
}

}  // namespace ModemCommands
