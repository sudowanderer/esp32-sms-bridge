#pragma once

#include <stddef.h>
#include <stdint.h>

namespace ModemCommands {

const char* attention();
const char* smsPduMode();
const char* smsDirectUrcMode();
const char* queryRegistration();
const char* querySignal();
const char* queryModuleInfo();
const char* queryExtendedSignal();
const char* queryImsi();
const char* queryIccid();
const char* queryOwnNumber();
const char* queryOperator();
const char* queryPdpActivation();
const char* queryPdpContext();
const char* queryMipCall();
const char* disconnectMipCall();

bool buildReadStoredSms(uint16_t index, char* output, size_t outputSize);
bool buildDeleteStoredSms(uint16_t index, char* output, size_t outputSize);

}  // namespace ModemCommands
