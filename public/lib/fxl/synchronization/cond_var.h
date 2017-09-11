// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// A condition variable class (to be used with |fxl::Mutex|).

#ifndef LIB_FXL_SYNCHRONIZATION_COND_VAR_H_
#define LIB_FXL_SYNCHRONIZATION_COND_VAR_H_

#include "lib/fxl/build_config.h"

#if defined(OS_WIN)
#include <windows.h>
#else
#include <pthread.h>
#endif
#include <stdint.h>

#include "lib/fxl/fxl_export.h"
#include "lib/fxl/macros.h"
#include "lib/fxl/synchronization/thread_annotations.h"
#include "lib/fxl/time/time_delta.h"

namespace fxl {

class Mutex;

class FXL_EXPORT CondVar final {
 public:
  CondVar();
  ~CondVar();

  // Atomically releases |*mutex| (which must be held) and blocks on this
  // condition variable, unblocking and reacquiring |*mutex| when:
  //   * |SignalAll()| is called,
  //   * |Signal()| is called and this thread is scheduled to be the next to be
  //     unblocked, or
  //   * whenever (spuriously, e.g., due to |EINTR|).
  // To deal with spurious wakeups, wait using a loop (with |my_mutex| held):
  //   while (!<my_condition>)
  //     cv.Wait(&my_mutex);
  void Wait(Mutex* mutex) FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex);

  // Like |Wait()|, but will also unblock when |timeout_microseconds| have
  // elapsed without this condition variable being signaled. Returns true on
  // timeout; this is somewhat counterintuitive, but the false case is
  // non-specific: the condition variable may or may not have been signaled and
  // |timeout_microseconds| may or may not have already elapsed (spurious
  // wakeups are possible).
  // TODO(vtl): A version with an absolute deadline time would be more efficient
  // for users who want to wait to be signaled or a timeout to have definitely
  // elapsed. With this API, users have to recalculate the timeout when they
  // detect a spurious wakeup.
  bool WaitWithTimeout(Mutex* mutex, TimeDelta timeout)
      FXL_EXCLUSIVE_LOCKS_REQUIRED(mutex);

  // Signals this condition variable, waking at least one waiting thread if
  // there are any.
  void Signal();

  // Signals this condition variable, waking all waiting threads.
  void SignalAll();

 private:
#if defined(OS_WIN)
  CONDITION_VARIABLE cv_;
#else
  pthread_cond_t impl_;
#endif

  FXL_DISALLOW_COPY_AND_ASSIGN(CondVar);
};

}  // namespace fxl

#endif  // LIB_FXL_SYNCHRONIZATION_COND_VAR_H_
