// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/input_dispatcher_impl.h"

namespace machina {

InputEventQueue::InputEventQueue(size_t queue_depth)
    : pending_(queue_depth) {}

size_t InputEventQueue::size() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return size_;
}

void InputEventQueue::PostEvent(fuchsia::ui::input::InputEvent event) {
  std::lock_guard<std::mutex> lock(mutex_);
  WriteEventToRingLocked(std::move(event));
  cv_.notify_one();
}

fuchsia::ui::input::InputEvent InputEventQueue::Wait() {
  std::unique_lock<std::mutex> lock(mutex_);
  while (size_ == 0) {
    cv_.wait(lock);
  }
  auto result = std::move(pending_[index_]);
  DropOldestLocked();
  size_--;
  return result;
}

void InputEventQueue::WriteEventToRingLocked(
    fuchsia::ui::input::InputEvent event) {
  pending_[(index_ + size_) % pending_.size()] = std::move(event);
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

void InputDispatcherImpl::DispatchEvent(fuchsia::ui::input::InputEvent event) {
  switch (event.Which()) {
    case fuchsia::ui::input::InputEvent::Tag::kKeyboard: {
      keyboard_.PostEvent(std::move(event));
      break;
    }
    case fuchsia::ui::input::InputEvent::Tag::kPointer: {
      switch (event.pointer().type) {
        case fuchsia::ui::input::PointerEventType::MOUSE: {
          mouse_.PostEvent(std::move(event));
          break;
        }
        case fuchsia::ui::input::PointerEventType::TOUCH:
        case fuchsia::ui::input::PointerEventType::STYLUS: {
          touch_.PostEvent(std::move(event));
          break;
        }
        default:
          break;
      }
    }
    default:
      break;
  }
}

}  // namespace machina
