// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <lib/closure-queue/closure_queue.h>
#include <zircon/assert.h>

// ThreadSafeDeleter
//
// This "holder" class is for holding instances of classes which must only be used on a single
// thread, but which are safe to curry to other threads (and back) between usages.  This also means
// the held instance must be safe to delete on any thread after it has been moved out.
//
// This class holds an instance of a moveable type, and ensures that the not-moved-out instance
// gets deleted on the correct thread, even if the destructor of the holder is called on the wrong
// thread.
//
// One use case:
//
// HLCPP FIDL callbacks are affinitized to the FIDL thread on which they're created.  They must
// only be deleted on the FIDL-handling thread they were created on.  Sometimes in normal operation
// it's convenient to curry a FIDL callback to another thread, then back to the FIDL thread to get
// called and deleted.  However, when shutting down, the currying can be cut short and the lambda
// currying the callback can be deleted on the wrong thread.
template<typename Held>
class ThreadSafeDeleter {
 public:
  // closure_queue - a ClosureQueue that'll out-last the ThreadSafeDeleter, which can be used to
  // run the held's destructor on the correct thread.
  ThreadSafeDeleter(ClosureQueue* closure_queue, Held&& held);

  ~ThreadSafeDeleter();

  // move-only, no copy
  ThreadSafeDeleter(ThreadSafeDeleter&& other);
  ThreadSafeDeleter& operator=(ThreadSafeDeleter&& other);
  ThreadSafeDeleter(const ThreadSafeDeleter& other) = delete;
  ThreadSafeDeleter& operator=(const ThreadSafeDeleter& other) = delete;

  [[nodiscard]]
  Held& held();
 private:
  void DeleteHeld();

  ClosureQueue* closure_queue_ = nullptr;
  // If ~ThreadSafeDeleter runs on the wrong thread, then the Held will be moved out, curried
  // over to the correct thread, and ~Held run there.
  Held held_;
  bool is_moved_out_ = false;
};

template<typename Held>
ThreadSafeDeleter<Held>::ThreadSafeDeleter(ClosureQueue* closure_queue, Held&& held)
  : closure_queue_(closure_queue),
    held_(std::move(held)) {
  ZX_DEBUG_ASSERT(closure_queue_);
  ZX_DEBUG_ASSERT(!is_moved_out_);
}

template<typename Held>
ThreadSafeDeleter<Held>::~ThreadSafeDeleter() {
  DeleteHeld();
}

template<typename Held>
ThreadSafeDeleter<Held>::ThreadSafeDeleter(ThreadSafeDeleter&& other)
  : closure_queue_(other.closure_queue_),
    held_(std::move(other.held_))
{
  ZX_DEBUG_ASSERT(!other.is_moved_out_);
  other.is_moved_out_ = true;
  ZX_DEBUG_ASSERT(!is_moved_out_);
}

template<typename Held>
ThreadSafeDeleter<Held>& ThreadSafeDeleter<Held>::operator=(ThreadSafeDeleter&& other) {
  ZX_DEBUG_ASSERT(!other.is_moved_out_);
  // Prevent this for now since we don't need it.  Not fundamentally invalid, but also not great
  // practice for the caller to do this, so let's not.
  ZX_DEBUG_ASSERT(!is_moved_out_);
  DeleteHeld();
  closure_queue_ = other.closure_queue_;
  held_ = std::move(other.held_);
  other.is_moved_out_ = true;
}

template<typename Held>
Held& ThreadSafeDeleter<Held>::held() {
  ZX_DEBUG_ASSERT(!is_moved_out_);
  return held_;
}

template<typename Held>
void ThreadSafeDeleter<Held>::DeleteHeld() {
  if (is_moved_out_) {
    return;
  }
  if (thrd_current() != closure_queue_->dispatcher_thread()) {
    closure_queue_->Enqueue([target_thread = closure_queue_->dispatcher_thread(),
                            held = std::move(held_)]{
      ZX_DEBUG_ASSERT(thrd_current() == target_thread);
      // ~held, on correct thread
    });
  }
}
