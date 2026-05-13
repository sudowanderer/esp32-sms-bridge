#include "sms_storage_reader_core.h"

#include <stdio.h>
#include <string.h>

void SmsStorageReaderCore::begin() {
  memset(queue_, 0, sizeof(queue_));
  memset(deleteQueue_, 0, sizeof(deleteQueue_));
  memset(concatGroups_, 0, sizeof(concatGroups_));
  head_ = 0;
  count_ = 0;
  deleteHead_ = 0;
  deleteCount_ = 0;
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

bool SmsStorageReaderCore::messageAccepted(const SmsMessage& message, bool queued) {
  if (activeState_ != ActiveState::AwaitingMessageQueue) {
    return false;
  }

  if (message.isConcat && message.concatTotal > 1) {
    if (!rememberConcatPart(message)) {
      clearActive();
      return false;
    }

    if (queued) {
      (void)enqueueConcatDeletes(message);
    }

    clearActive();
    return true;
  }

  if (queued) {
    (void)enqueueDelete(active_);
  }

  clearActive();
  return true;
}

bool SmsStorageReaderCore::messageQueued(char* deleteCommand, size_t deleteCommandCapacity) {
  if (activeState_ == ActiveState::AwaitingMessageQueue) {
    (void)enqueueDelete(active_);
    clearActive();
  }

  return nextDeleteCommand(deleteCommand, deleteCommandCapacity);
}

bool SmsStorageReaderCore::nextDeleteCommand(char* deleteCommand, size_t deleteCommandCapacity) {
  if (activeState_ != ActiveState::None || deleteCount_ == 0 || deleteCommand == nullptr || deleteCommandCapacity == 0) {
    return false;
  }

  active_ = deleteQueue_[deleteHead_];
  deleteHead_ = static_cast<uint8_t>((deleteHead_ + 1) % kDeleteCapacity);
  deleteCount_--;
  activeState_ = ActiveState::Deleting;
  snprintf(deleteCommand, deleteCommandCapacity, "AT+CMGD=%u", static_cast<unsigned>(active_.index));
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

bool SmsStorageReaderCore::rememberConcatPart(const SmsMessage& message) {
  if (message.concatPart == 0 || message.concatTotal == 0 || message.concatPart > message.concatTotal ||
      message.concatTotal > kMaxConcatParts) {
    return false;
  }

  int groupIndex = findConcatGroup(message);
  if (groupIndex < 0) {
    groupIndex = findFreeConcatGroup();
    if (groupIndex < 0) {
      emit(SmsStorageReaderEvent::QueueFull, "concat_group_full");
      return false;
    }

    ConcatGroup& group = concatGroups_[groupIndex];
    memset(&group, 0, sizeof(group));
    group.inUse = true;
    copyText(group.storage, sizeof(group.storage), active_.storage);
    copyText(group.sender, sizeof(group.sender), message.sender);
    group.ref = message.concatRef;
    group.total = message.concatTotal;
  }

  ConcatGroup& group = concatGroups_[groupIndex];
  const uint8_t partIndex = static_cast<uint8_t>(message.concatPart - 1);
  group.partValid[partIndex] = true;
  group.partIndex[partIndex] = active_.index;
  return true;
}

bool SmsStorageReaderCore::enqueueConcatDeletes(const SmsMessage& message) {
  const int groupIndex = findConcatGroup(message);
  if (groupIndex < 0) {
    return false;
  }

  ConcatGroup& group = concatGroups_[groupIndex];
  bool ok = true;
  for (uint8_t i = 0; i < group.total; ++i) {
    if (!group.partValid[i]) {
      continue;
    }

    StoredSms storedSms = {};
    copyText(storedSms.storage, sizeof(storedSms.storage), group.storage);
    storedSms.index = group.partIndex[i];
    ok = enqueueDelete(storedSms) && ok;
  }

  clearConcatGroup(static_cast<uint8_t>(groupIndex));
  return ok;
}

bool SmsStorageReaderCore::enqueueDelete(const StoredSms& storedSms) {
  if (storedSms.index == 0 || storedSms.storage[0] == '\0') {
    return false;
  }

  for (uint8_t i = 0; i < deleteCount_; ++i) {
    const uint8_t index = static_cast<uint8_t>((deleteHead_ + i) % kDeleteCapacity);
    if (deleteQueue_[index].index == storedSms.index && strcmp(deleteQueue_[index].storage, storedSms.storage) == 0) {
      return true;
    }
  }

  if (activeState_ == ActiveState::Deleting && active_.index == storedSms.index &&
      strcmp(active_.storage, storedSms.storage) == 0) {
    return true;
  }

  if (deleteCount_ >= kDeleteCapacity) {
    const StoredSms previousActive = active_;
    const ActiveState previousState = activeState_;
    active_ = storedSms;
    emit(SmsStorageReaderEvent::QueueFull, "delete_queue_full");
    active_ = previousActive;
    activeState_ = previousState;
    return false;
  }

  const uint8_t index = static_cast<uint8_t>((deleteHead_ + deleteCount_) % kDeleteCapacity);
  deleteQueue_[index] = storedSms;
  deleteCount_++;
  return true;
}

int SmsStorageReaderCore::findConcatGroup(const SmsMessage& message) const {
  for (uint8_t i = 0; i < kMaxConcatGroups; ++i) {
    const ConcatGroup& group = concatGroups_[i];
    if (group.inUse && strcmp(group.storage, active_.storage) == 0 && strcmp(group.sender, message.sender) == 0 &&
        group.ref == message.concatRef && group.total == message.concatTotal) {
      return i;
    }
  }

  return -1;
}

int SmsStorageReaderCore::findFreeConcatGroup() const {
  for (uint8_t i = 0; i < kMaxConcatGroups; ++i) {
    if (!concatGroups_[i].inUse) {
      return i;
    }
  }

  return -1;
}

void SmsStorageReaderCore::clearConcatGroup(uint8_t groupIndex) {
  if (groupIndex >= kMaxConcatGroups) {
    return;
  }

  memset(&concatGroups_[groupIndex], 0, sizeof(concatGroups_[groupIndex]));
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
