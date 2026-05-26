#include "modem_events.h"

static ModemEventsCore modemEventsCore;

void modemEventsBegin() {
  modemEventsCore.begin();
}

bool modemEventsOnUrc(const char* line, uint32_t nowMs) {
  return modemEventsCore.onUrc(line, nowMs);
}

void modemEventsPoll(uint32_t nowMs) {
  modemEventsCore.poll(nowMs);
}

ModemEventsSnapshot modemEventsGet() {
  return modemEventsCore.snapshot();
}
