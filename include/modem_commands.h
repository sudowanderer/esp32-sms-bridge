#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ModemCommands {

const char* attention();
const char* smsPduMode();
const char* smsDirectUrcMode();
const char* queryRegistration();
const char* querySignal();

bool buildReadStoredSms(uint16_t index, char* output, size_t outputSize);
bool buildDeleteStoredSms(uint16_t index, char* output, size_t outputSize);

}  // namespace ModemCommands
