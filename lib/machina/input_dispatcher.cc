// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/input_dispatcher.h"

namespace machina {

static constexpr InputEvent kBarrierEvent = {
    .type = InputEventType::BARRIER,
};

InputEventQueue::InputEventQueue(size_t queue_depth)
    : pending_(new InputEvent[queue_depth], queue_depth) {}

size_t InputEventQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return size_;
}

void InputEventQueue::PostEvent(const InputEvent& event, bool flush) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    WriteEventToRingLocked(event);
    if (flush) {
      WriteEventToRingLocked(kBarrierEvent);
    }
  }
  cv_.notify_one();
}

InputEvent InputEventQueue::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (size_ == 0) {
    cv_.wait(lock);
  }
  InputEvent result = pending_[index_];
  DropOldestLocked();
  size_--;
  return result;
}

void InputEventQueue::WriteEventToRingLocked(const InputEvent& event) {
  pending_[(index_ + size_) % pending_.size()] = event;
  if (size_ < pending_.size()) {
    size_++;
  } else {
    // Ring is full.
    DropOldestLocked();
  }
}

void InputEventQueue::DropOldestLocked() {
  index_ = (index_ + 1) % pending_.size();
}

}  // namespace machina
