// Copyright 2018 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_STATE_H_
#define ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_STATE_H_

#include <assert.h>

// The state of a thread has two axes. One is its arc from birth to
// death, expressed via its Lifecycle value. The other its current
// exception handler state, expressed via its Exception value.

// This class exists to encapsulate all legal state transitions, so
// generally other assertions about the state of a thread prior to
// transitioning are not necessary.

// Only RUNNING, SUSPENDED, or DYING threads may participate in
// exception handling.

class ThreadState {
 public:
  // The only legal transition that isn't from top-to-bottom occurs
  // when a thread is resumed after being suspended.
  enum class Lifecycle {
    // The ThreadDispatcher has been allocated, but not yet
    // associated to a Thread or an aspace.
    INITIAL,

    // The ThreadDispatcher is now associated to its underlying
    // Thread and the containing process's address space, and is
    // waiting to be run.
    INITIALIZED,

    // The thread is running.
    RUNNING,

    // The thread is currently suspended.
    // Note that suspension is orthogonal to being "in an exception".
    // A thread may be both suspended and in an exception, and the thread
    // does not "resume" execution until it is resumed from both the
    // suspension and the exception.
    SUSPENDED,

    // The thread is going to die. It may still be interacting
    // with exception handling state.
    DYING,

    // The thread is being dissociated from all of its state, and
    // no more interaction with userspace (including exception
    // handlers) is possible.
    DEAD,
  };

  // IDLE threads become UNPROCESSED. UNPROCESSED threads are told
  // to either RESUME or TRY_NEXT in a loop until they are killed
  // (no more exception handlers) or resumed, in which case they
  // become IDLE again.
  enum class Exception {
    // There's no pending exception.
    IDLE,

    // The thread is waiting for the pending exception to be
    // processed.
    UNPROCESSED,

    // The exception has been processed, and the next exception
    // handler should be queried.
    TRY_NEXT,

    // The exception has been processed, and the thread should
    // resume.
    RESUME,
  };

  Lifecycle lifecycle() const {
    switch (value_) {
      case Value::INITIAL_IDLE:
        return Lifecycle::INITIAL;
      case Value::INITIALIZED_IDLE:
        return Lifecycle::INITIALIZED;
      case Value::RUNNING_IDLE:
      case Value::RUNNING_UNPROCESSED:
      case Value::RUNNING_TRY_NEXT:
      case Value::RUNNING_RESUME:
        return Lifecycle::RUNNING;
      case Value::SUSPENDED_IDLE:
      case Value::SUSPENDED_UNPROCESSED:
      case Value::SUSPENDED_TRY_NEXT:
      case Value::SUSPENDED_RESUME:
        return Lifecycle::SUSPENDED;
      case Value::DYING_IDLE:
      case Value::DYING_UNPROCESSED:
      case Value::DYING_TRY_NEXT:
      case Value::DYING_RESUME:
        return Lifecycle::DYING;
      case Value::DEAD_IDLE:
        return Lifecycle::DEAD;
      default:
        ASSERT(false);
    }
  }

  // Only RUNNING, SUSPENDED, and DYING threads have meaningful
  // exception state.
  Exception exception() const {
    switch (value_) {
      case Value::RUNNING_IDLE:
      case Value::SUSPENDED_IDLE:
      case Value::DYING_IDLE:
        return Exception::IDLE;
      case Value::RUNNING_UNPROCESSED:
      case Value::SUSPENDED_UNPROCESSED:
      case Value::DYING_UNPROCESSED:
        return Exception::UNPROCESSED;
      case Value::RUNNING_TRY_NEXT:
      case Value::SUSPENDED_TRY_NEXT:
      case Value::DYING_TRY_NEXT:
        return Exception::TRY_NEXT;
      case Value::RUNNING_RESUME:
      case Value::SUSPENDED_RESUME:
      case Value::DYING_RESUME:
        return Exception::RESUME;
      case Value::INITIALIZED_IDLE:
      case Value::DEAD_IDLE:
        // Someone could have, for example, requested zx_info_thread_t.
        return Exception::IDLE;
      default:
        ASSERT(false);
    }
  }

  void set(Lifecycle lifecycle) {
    switch (lifecycle) {
      case Lifecycle::INITIAL:
        DEBUG_ASSERT(false);
        return;
      case Lifecycle::INITIALIZED:
        switch (value_) {
          case Value::INITIAL_IDLE:
            value_ = Value::INITIALIZED_IDLE;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Lifecycle::RUNNING:
        switch (value_) {
          case Value::INITIALIZED_IDLE:
          case Value::SUSPENDED_IDLE:
            value_ = Value::RUNNING_IDLE;
            return;
          case Value::SUSPENDED_UNPROCESSED:
            value_ = Value::RUNNING_UNPROCESSED;
            return;
          case Value::SUSPENDED_TRY_NEXT:
            value_ = Value::RUNNING_TRY_NEXT;
            return;
          case Value::SUSPENDED_RESUME:
            value_ = Value::RUNNING_RESUME;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Lifecycle::SUSPENDED:
        switch (value_) {
          case Value::RUNNING_IDLE:
            value_ = Value::SUSPENDED_IDLE;
            return;
          case Value::RUNNING_UNPROCESSED:
            value_ = Value::SUSPENDED_UNPROCESSED;
            return;
          case Value::RUNNING_TRY_NEXT:
            value_ = Value::SUSPENDED_TRY_NEXT;
            return;
          case Value::RUNNING_RESUME:
            value_ = Value::SUSPENDED_RESUME;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Lifecycle::DYING:
        switch (value_) {
          case Value::RUNNING_IDLE:
          case Value::SUSPENDED_IDLE:
          case Value::DYING_IDLE:
            value_ = Value::DYING_IDLE;
            return;
          case Value::RUNNING_UNPROCESSED:
          case Value::SUSPENDED_UNPROCESSED:
          case Value::DYING_UNPROCESSED:
            value_ = Value::DYING_UNPROCESSED;
            return;
          case Value::RUNNING_TRY_NEXT:
          case Value::SUSPENDED_TRY_NEXT:
          case Value::DYING_TRY_NEXT:
            value_ = Value::DYING_TRY_NEXT;
            return;
          case Value::RUNNING_RESUME:
          case Value::SUSPENDED_RESUME:
          case Value::DYING_RESUME:
            value_ = Value::DYING_RESUME;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Lifecycle::DEAD:
        switch (value_) {
          case Value::DYING_IDLE:
          case Value::DYING_UNPROCESSED:
          case Value::DYING_TRY_NEXT:
          case Value::DYING_RESUME:
            value_ = Value::DEAD_IDLE;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
    }
  }

  void set(Exception exception) {
    switch (exception) {
      case Exception::IDLE:
        switch (value_) {
          case Value::RUNNING_UNPROCESSED:
          case Value::RUNNING_TRY_NEXT:
          case Value::RUNNING_RESUME:
            value_ = Value::RUNNING_IDLE;
            return;
          case Value::SUSPENDED_UNPROCESSED:
          case Value::SUSPENDED_TRY_NEXT:
          case Value::SUSPENDED_RESUME:
            value_ = Value::SUSPENDED_IDLE;
            return;
          case Value::DYING_UNPROCESSED:
          case Value::DYING_TRY_NEXT:
          case Value::DYING_RESUME:
            value_ = Value::DYING_IDLE;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Exception::UNPROCESSED:
        switch (value_) {
          case Value::RUNNING_IDLE:
            value_ = Value::RUNNING_UNPROCESSED;
            return;
          case Value::SUSPENDED_IDLE:
            value_ = Value::SUSPENDED_UNPROCESSED;
            return;
          case Value::DYING_IDLE:
            value_ = Value::DYING_UNPROCESSED;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Exception::TRY_NEXT:
        switch (value_) {
          case Value::RUNNING_UNPROCESSED:
            value_ = Value::RUNNING_TRY_NEXT;
            return;
          case Value::SUSPENDED_UNPROCESSED:
            value_ = Value::SUSPENDED_TRY_NEXT;
            return;
          case Value::DYING_UNPROCESSED:
            value_ = Value::DYING_TRY_NEXT;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
      case Exception::RESUME:
        switch (value_) {
          case Value::RUNNING_UNPROCESSED:
            value_ = Value::RUNNING_RESUME;
            return;
          case Value::SUSPENDED_UNPROCESSED:
            value_ = Value::SUSPENDED_RESUME;
            return;
          case Value::DYING_UNPROCESSED:
            value_ = Value::DYING_RESUME;
            return;
          default:
            DEBUG_ASSERT(false);
            return;
        }
    }
  }

 private:
  enum class Value {
    INITIAL_IDLE,

    INITIALIZED_IDLE,

    RUNNING_IDLE,
    RUNNING_UNPROCESSED,
    RUNNING_TRY_NEXT,
    RUNNING_RESUME,

    SUSPENDED_IDLE,
    SUSPENDED_UNPROCESSED,
    SUSPENDED_TRY_NEXT,
    SUSPENDED_RESUME,

    DYING_IDLE,
    DYING_UNPROCESSED,
    DYING_TRY_NEXT,
    DYING_RESUME,

    DEAD_IDLE,
  };

  Value value_ = Value::INITIAL_IDLE;
};

const char* ThreadLifecycleToString(ThreadState::Lifecycle lifecycle);

#endif  // ZIRCON_KERNEL_OBJECT_INCLUDE_OBJECT_THREAD_STATE_H_
