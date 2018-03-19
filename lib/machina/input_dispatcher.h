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
  BARRIER,
  POINTER,
  BUTTON,
};

enum class KeyState {
  PRESSED,
  RELEASED,
};

struct KeyEvent {
  uint32_t hid_usage;
  KeyState state;
};

enum class PointerType {
  RELATIVE,
  ABSOLUTE,
};

struct PointerEvent {
  float x;
  float y;
  PointerType type;
};

enum class Button {
  BTN_MOUSE_PRIMARY,
  BTN_MOUSE_SECONDARY,
  BTN_MOUSE_TERTIARY,
};

struct ButtonEvent {
  Button button;
  KeyState state;
};

struct InputEvent {
  InputEventType type;
  union {
    KeyEvent key;
    PointerEvent pointer;
    ButtonEvent button;
  };
};

class InputEventQueue {
 public:
  InputEventQueue(size_t queue_depth);

  // Adds an input event the the queue. If the queue is full the oldest element
  // will be overwritten.
  void PostEvent(const InputEvent& event, bool flush = false);

  // Blocks until an InputEvent is available.
  InputEvent Wait();

  size_t size() const;

 private:
  void WriteEventToRingLocked(const InputEvent&) __TA_REQUIRES(mutex_);
  void DropOldestLocked() __TA_REQUIRES(mutex_);

  mutable fbl::Mutex mutex_;
  cnd_t signal_;
  fbl::Array<InputEvent> pending_ __TA_GUARDED(mutex_);
  size_t index_ __TA_GUARDED(mutex_) = 0;
  size_t size_ __TA_GUARDED(mutex_) = 0;
};

// The InputDispatcher maintains InputEventQueues of pending InputEvents. This
// class serves as a point of indirection between components that generate input
// events, and devices that consume them.
class InputDispatcher {
 public:
  InputDispatcher(size_t queue_depth)
      : keyboard_(queue_depth), mouse_(queue_depth), touch_(queue_depth) {}

  InputEventQueue* Keyboard() { return &keyboard_; }
  InputEventQueue* Mouse() { return &mouse_; }
  InputEventQueue* Touch() { return &touch_; }

 private:
  InputEventQueue keyboard_;
  InputEventQueue mouse_;
  InputEventQueue touch_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_INPUT_DISPATCHER_H_
