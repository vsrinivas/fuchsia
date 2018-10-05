// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_MACHINA_INPUT_DISPATCHER_IMPL_H_
#define GARNET_LIB_MACHINA_INPUT_DISPATCHER_IMPL_H_

#include <condition_variable>
#include <mutex>
#include <vector>

#include <fuchsia/ui/input/cpp/fidl.h>

namespace machina {

class InputEventQueue {
 public:
  InputEventQueue(size_t queue_depth);

  // Adds an input event the the queue. If the queue is full the oldest element
  // will be overwritten.
  void PostEvent(fuchsia::ui::input::InputEvent event);

  // Blocks until an InputEvent is available.
  fuchsia::ui::input::InputEvent Wait() __TA_NO_THREAD_SAFETY_ANALYSIS;

  size_t size() const;

 private:
  void WriteEventToRingLocked(fuchsia::ui::input::InputEvent event)
      __TA_REQUIRES(mutex_);
  void DropOldestLocked() __TA_REQUIRES(mutex_);

  std::condition_variable cv_;
  mutable std::mutex mutex_;
  std::vector<fuchsia::ui::input::InputEvent> pending_ __TA_GUARDED(mutex_);
  size_t index_ __TA_GUARDED(mutex_) = 0;
  size_t size_ __TA_GUARDED(mutex_) = 0;
};

// The InputDispatcher maintains InputEventQueues of pending InputEvents. This
// class serves as a point of indirection between components that generate input
// events, and devices that consume them.
class InputDispatcherImpl : public fuchsia::ui::input::InputDispatcher {
 public:
  InputDispatcherImpl(size_t queue_depth);

  InputEventQueue* queue() { return &queue_; }

  // |InputDispatcher|
  void DispatchEvent(fuchsia::ui::input::InputEvent event) override;

 private:
  InputEventQueue queue_;
};

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_INPUT_DISPATCHER_IMPL_H_
