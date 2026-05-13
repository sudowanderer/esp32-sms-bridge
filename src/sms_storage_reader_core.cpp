#include "sms_storage_reader_core.h"

#include <stdio.h>
#include <string.h>

void SmsStorageReaderCore::begin() {
  memset(queue_, 0, sizeof(queue_));
  head_ = 0;
  count_ = 0;
  clearActive();
  statusCallback_ = nullptr;
  statusUserData_ = nullptr;
}

void SmsStorageReaderCore::setStatusCallback(SmsStorageReaderStatusCallback callback, void* userData) {
  statusCallback_ = callback;
  statusUserData_ = userData;
}

bool SmsStorageReaderCore::enqueue(const SmsStorageNotification& notification) {
  if (notification.index == 0 || notification.storage[0] == '\0') {
    return false;
  }

  if (hasQueuedIndex(notification)) {
    return true;
  }

  if (count_ >= kCapacity) {
    const StoredSms previousActive = active_;
    const ActiveState previousState = activeState_;
    active_.index = notification.index;
    copyText(active_.storage, sizeof(active_.storage), notification.storage);
    emit(SmsStorageReaderEvent::QueueFull, "queue_full");
    active_ = previousActive;
    activeState_ = previousState;
    return false;
  }

  const uint8_t index = static_cast<uint8_t>((head_ + count_) % kCapacity);
  copyText(queue_[index].storage, sizeof(queue_[index].storage), notification.storage);
  queue_[index].index = notification.index;
  count_++;

  const StoredSms previousActive = active_;
  const ActiveState previousState = activeState_;
  active_ = queue_[index];
  emit(SmsStorageReaderEvent::Enqueued, "");
  active_ = previousActive;
  activeState_ = previousState;
  return true;
}

bool SmsStorageReaderCore::nextReadCommand(char* command, size_t commandCapacity) {
  if (activeState_ != ActiveState::None || count_ == 0 || command == nullptr || commandCapacity == 0) {
    return false;
  }

  active_ = queue_[head_];
  head_ = static_cast<uint8_t>((head_ + 1) % kCapacity);
  count_--;
  activeState_ = ActiveState::Reading;
  snprintf(command, commandCapacity, "AT+CMGR=%u", static_cast<unsigned>(active_.index));
  emit(SmsStorageReaderEvent::ReadCommandReady, command);
  return true;
}

bool SmsStorageReaderCore::completeRead(ModemAtResult result, const char* response, char* pdu, size_t pduCapacity) {
  if (activeState_ != ActiveState::Reading || pdu == nullptr || pduCapacity == 0) {
    return false;
  }

  pdu[0] = '\0';

  if (result != ModemAtResult::Ok) {
    emit(SmsStorageReaderEvent::ReadFailed, "at_failed");
    clearActive();
    return false;
  }

  if (!extractPduLine(response, pdu, pduCapacity)) {
    emit(SmsStorageReaderEvent::ReadFailed, "pdu_missing");
    clearActive();
    return false;
  }

  activeState_ = ActiveState::AwaitingMessageQueue;
  emit(SmsStorageReaderEvent::PduReady, "pdu_ready");
  return true;
}

bool SmsStorageReaderCore::messageQueued(char* deleteCommand, size_t deleteCommandCapacity) {
  if (activeState_ != ActiveState::AwaitingMessageQueue || deleteCommand == nullptr || deleteCommandCapacity == 0) {
    return false;
  }

  snprintf(deleteCommand, deleteCommandCapacity, "AT+CMGD=%u", static_cast<unsigned>(active_.index));
  activeState_ = ActiveState::Deleting;
  emit(SmsStorageReaderEvent::DeleteCommandReady, deleteCommand);
  return true;
}

void SmsStorageReaderCore::messageRejected() {
  if (activeState_ == ActiveState::AwaitingMessageQueue) {
    clearActive();
  }
}

void SmsStorageReaderCore::completeDelete(ModemAtResult result) {
  if (activeState_ != ActiveState::Deleting) {
    return;
  }

  emit(result == ModemAtResult::Ok ? SmsStorageReaderEvent::DeleteSucceeded : SmsStorageReaderEvent::DeleteFailed,
       result == ModemAtResult::Ok ? "" : "at_failed");
  clearActive();
}

bool SmsStorageReaderCore::isActive() const {
  return activeState_ != ActiveState::None;
}

uint8_t SmsStorageReaderCore::queuedCount() const {
  return count_;
}

void SmsStorageReaderCore::copyText(char* dest, size_t destCapacity, const char* source) {
  if (dest == nullptr || destCapacity == 0) {
    return;
  }

  if (source == nullptr) {
    dest[0] = '\0';
    return;
  }

  strncpy(dest, source, destCapacity - 1);
  dest[destCapacity - 1] = '\0';
}

bool SmsStorageReaderCore::extractPduLine(const char* response, char* pdu, size_t pduCapacity) {
  if (response == nullptr || pdu == nullptr || pduCapacity == 0) {
    return false;
  }

  const char* lineStart = response;
  while (*lineStart != '\0') {
    const char* lineEnd = strchr(lineStart, '\n');
    const size_t lineLen = lineEnd == nullptr ? strlen(lineStart) : static_cast<size_t>(lineEnd - lineStart);
    if (lineLen > 0 && lineLen < pduCapacity) {
      bool hex = true;
      for (size_t i = 0; i < lineLen; ++i) {
        const char c = lineStart[i];
        const bool isHex = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
        if (!isHex) {
          hex = false;
          break;
        }
      }

      if (hex && (lineLen % 2) == 0) {
        memcpy(pdu, lineStart, lineLen);
        pdu[lineLen] = '\0';
        return true;
      }
    }

    if (lineEnd == nullptr) {
      break;
    }
    lineStart = lineEnd + 1;
  }

  return false;
}

void SmsStorageReaderCore::emit(SmsStorageReaderEvent event, const char* detail) {
  if (statusCallback_ == nullptr) {
    return;
  }

  SmsStorageReaderStatus status = {};
  status.event = event;
  copyText(status.storage, sizeof(status.storage), active_.storage);
  status.index = active_.index;
  copyText(status.detail, sizeof(status.detail), detail);
  statusCallback_(status, statusUserData_);
}

void SmsStorageReaderCore::clearActive() {
  memset(&active_, 0, sizeof(active_));
  activeState_ = ActiveState::None;
}

bool SmsStorageReaderCore::hasQueuedIndex(const SmsStorageNotification& notification) const {
  if (activeState_ != ActiveState::None && active_.index == notification.index &&
      strcmp(active_.storage, notification.storage) == 0) {
    return true;
  }

  for (uint8_t i = 0; i < count_; ++i) {
    const uint8_t index = static_cast<uint8_t>((head_ + i) % kCapacity);
    if (queue_[index].index == notification.index && strcmp(queue_[index].storage, notification.storage) == 0) {
      return true;
    }
  }

  return false;
}
