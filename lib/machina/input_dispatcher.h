// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_INPUT_DISPATCHER_H_
#define GARNET_LIB_MACHINA_INPUT_DISPATCHER_H_

#include <threads.h>

#include <fbl/array.h>
#include <fbl/mutex.h>

namespace machina {

enum class InputEventType {
  KEYBOARD,
};

enum class KeyState {
  PRESSED,
  RELEASED,
};

struct KeyEvent {
  uint32_t hid_usage;
  KeyState state;
};

struct InputEvent {
  InputEventType type;
  union {
    KeyEvent key;
  };
};

// The InputDispatcher maintains a queue of pending InputEvents. This class
// serves as a point of indirection between components that generate input
// events, and devices that consume them.
class InputDispatcher {
 public:
  InputDispatcher(size_t queue_depth);

  // Adds an input event the the queue. If the queue is full the oldest element
  // will be overwritten.
  void PostEvent(const InputEvent event);

  // Blocks until an InputEvent is available.
  InputEvent Wait();

  size_t size() const;

 private:
  void DropOldestLocked() __TA_REQUIRES(mutex_);

  mutable fbl::Mutex mutex_;
  cnd_t signal_;
  fbl::Array<InputEvent> pending_ __TA_GUARDED(mutex_);
  size_t index_ __TA_GUARDED(mutex_) = 0;
  size_t size_ __TA_GUARDED(mutex_) = 0;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_INPUT_DISPATCHER_H_
