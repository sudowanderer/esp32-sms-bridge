#include "startup_commands.h"

#include "modem_commands.h"

namespace {

const char* const kStartupCommands[] = {
    ModemCommands::attention(),
    ModemCommands::deactivatePdpContext(),
    ModemCommands::smsPduMode(),
    ModemCommands::smsDirectUrcMode(),
    ModemCommands::queryRegistration(),
};

}  // namespace

namespace StartupCommands {

size_t count() {
  return sizeof(kStartupCommands) / sizeof(kStartupCommands[0]);
}

const char* at(size_t index) {
  if (index >= count()) {
    return nullptr;
  }
  return kStartupCommands[index];
}

}  // namespace StartupCommands
