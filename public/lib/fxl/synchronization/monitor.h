// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_FXL_SYNCHRONIZATION_MONITOR_H_
#define LIB_FXL_SYNCHRONIZATION_MONITOR_H_

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/synchronization/cond_var.h"
#include "lib/fxl/synchronization/mutex.h"

namespace fxl {

// A mechanism that combines mutual exclusion and waiting for a condition to be
// signalled.
//
// A monitor differs from an |AutoResetWaitableEvent| and an
// |ManualResetWaitableEvent| in that it doesn't contain any specific state
// (whereas a waitable event contains a Boolean value indicating whether the
// event has occurred). For this reason, a montior has separate |Enter|, |Wait|,
// and |Exit| operations to let clients manipulate state while holding the mutex
// (whereas a waitable event combines the Enter, Wait, and Exit sequence into a
// single operation).
//
// See <https://en.wikipedia.org/wiki/Monitor_(synchronization)> for more
// context.
class FXL_LOCKABLE FXL_EXPORT Monitor {
 public:
  Monitor();
  ~Monitor();

  // Gain exclusive access to the monitor. Rather than calling this function
  // directly, consider using |MonitorLocker| to ensure that you call |Exit| to
  // give up exclusive access to the monitor.
  void Enter() FXL_EXCLUSIVE_LOCK_FUNCTION();

  // Give up excusive access to the monitor. Rather than calling this function
  // directly, consider using |MonitorLocker| to pair this call with |Enter|.
  void Exit() FXL_UNLOCK_FUNCTION();

  // Signal that the condition associated with the monitor has occurred. This
  // function will wake up a thread that is waiting on the monitor in |Wait|.
  // (The call might wake up additional threads because the underlying condition
  // variable has spurious wakeups.)
  void Signal();

  // Wait until the condition associated with the monitor has occurred. This
  // function will sleep until |Signal| is called or until the underly condition
  // variable cause a spurious wakeup.
  //
  // Callers of this function should check the condition they are waiting on
  // after |Wait| returns because the . If the
  // condition is not
  // satisfied, they should call wait again:
  //
  //   while (!HaveSatisfiedCondition())
  //     monitor.Wait();
  //
  // The thread calling this function must already have entered the monitor,
  // either via |Enter| or by way of a |MonitorLocker|.
  void Wait() FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);

 private:
  CondVar cv_;
  Mutex mutex_;

  FXL_DISALLOW_COPY_AND_ASSIGN(Monitor);
};

// Helps ensure that calls to |Monitor::Enter| are paired with calls to
// |Monitor::Exit|. Typically allocated on the stack.
class FXL_EXPORT FXL_SCOPED_LOCKABLE MonitorLocker {
 public:
  // Enters the given monitor.
  explicit MonitorLocker(Monitor* monitor) FXL_ACQUIRE(monitor);

  // Exits the given monitor.
  ~MonitorLocker() FXL_UNLOCK_FUNCTION();

  // Calls |Wait| on the monitor given to this object's constructor.
  void Wait();

  // Calls |Signal| on the monitor given to this object's constructor.
  void Signal();

 private:
  Monitor* const monitor_;

  FXL_DISALLOW_COPY_AND_ASSIGN(MonitorLocker);
};

}  // namespace fxl

#endif  // LIB_FXL_SYNCHRONIZATION_MONITOR_H_
