// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_
#define SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_

#include <lib/fit/function.h>
#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <atomic>
#include <string_view>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

// Defines a function that takes a deadline and may return ZX_ERR_TIMED_OUT if that deadline is
// passed.
using Waiter = fit::function<zx_status_t(zx::time)>;

// Waiter functions

// Calls |waiter| with no deadline (i.e. the deadline is |zx::time::infinite|). Returns the result
// of calling |waiter|.
//
// If |waiter| completes within a configured threshold, no further action is taken. Otherwise, it
// logs a warning and logs an additional message if |waiter| eventually completes.
//
// Example:
//
//   auto waiter = [](zx::time deadline){
//     return channel.wait_one(ZX_CHANNEL_READABLE, deadline.get(), nullptr);
//   };
//   auto status = WaitFor("channel to become readable", &waiter);
//
zx_status_t WaitFor(const char* what, Waiter* waiter);

// Like |WaitFor|, but executes the waiter repeatedly after each |interval|. This allows creating
// |waiter|s that wait indefinitely for one condition, but can exit early by polling another.
zx_status_t PollFor(const char* what, Waiter* waiter, zx::duration interval);

// Configures the |threshold| after which |WaitFor| should log a warning. If the value is less than
// or equal to zero, logging is disabled. This should only be used for testing |WaitFor| and
// |SyncWait| themselves.
void SetThreshold(zx::duration threshold);

// Resets the |threshold| after which |WaitFor| should log a warning to the default value. This
// should only be used for testing |WaitFor| and |SyncWait| themselves.
void ResetThreshold();

// Equivalent to |SetThreshold(zx::duration(0)|. This disables all logging by |WaitFor|. Slow waits
// are logged by default for tests; other |main| functions may want to disable them since normal
// operation may involve slow waits (e.g. slow fuzzing iterations, waiting for the user, etc.).
void DisableSlowWaitLogging();

// This class is a thin wrapper around |sync_completion_t| that adds diagnostics when an indefinite
// wait exceeds a threshold, and ensures no waiters remain when the object goes out of scope.
class SyncWait final {
 public:
  SyncWait();
  ~SyncWait();

  bool is_signaled() const { return sync_completion_signaled(&sync_); }

  // Like |WaitFor| with a waiter that waits for this object to be |Signal|led.
  //
  // For example,
  //
  //   SyncWait sync;
  //   std::thread t([&](){
  //     zx::nanosleep(zx::deadline_after(zx::min(1)));
  //     sync.Signal();
  //   });
  //   sync.WaitFor("event to happen");
  //
  // will log something similar to:
  //
  //   WARNING: Still waiting for event to happen after 30 seconds...
  //   WARNING: Done waiting for event to happen after 60 seconds.
  //
  void WaitFor(const char* what);

  // Returns ZX_OK if |Signal|ed before |duration| elapses, or ZX_ERR_TIMED_OUT.
  zx_status_t TimedWait(zx::duration duration);

  // Returns ZX_OK if |Signal|ed before |deadline| is reached, or ZX_ERR_TIMED_OUT.
  zx_status_t WaitUntil(zx::time deadline);

  void Signal();

  void Reset();

 private:
  Waiter waiter_;
  sync_completion_t sync_;
  std::atomic<size_t> waiters_ = 0;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SyncWait);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_
