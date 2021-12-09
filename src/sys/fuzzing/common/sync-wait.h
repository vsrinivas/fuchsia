// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_
#define SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_

#include <lib/sync/completion.h>
#include <lib/zx/time.h>

#include <atomic>
#include <string_view>

#include "src/lib/fxl/macros.h"

namespace fuzzing {

// This class is a thin wrapper around |sync_completion_t| that adds diagnostics when an indefinite
// wait exceeds a threshold, and ensures no waiters remain when the object goes out of scope.
class SyncWait final {
 public:
  static constexpr zx::duration kDefaultThreshold{ZX_SEC(30)};

  SyncWait() = default;
  ~SyncWait();

  bool is_signaled() const { return sync_completion_signaled(&sync_); }
  zx::duration threshold() const { return threshold_; }

  // Sets the |threshold| after which |WaitFor| will produce diagnostic messages and
  // |has_exceeded_threshold| will return true until a call to |Reset|. A |threshold| less than or
  // equal to zero will disable the threshold checks.
  void set_threshold(zx::duration threshold) { threshold_ = threshold; }

  // Returns whether a call to |WaitFor| exceeded the configured threshold without a subsequent call
  // to |Reset|.
  bool has_exceeded_threshold() const { return exceeded_; }

  // Like |WaitUntil(zx::time::infinite())|, but logs a warning after the configured threshold if it
  // is positive, and a subsequent informational message if eventually |Signal|led.
  //
  // For example,
  //
  //   SyncWait sync;
  //   sync.set_threshold(zx::sec(3));
  //   std::thread t([&](){
  //     zx::nanosleep(zx::deadline_after(zx::sec(5)));
  //     sync.Signal();
  //   });
  //   sync.WaitFor("event to happen");
  //
  // will log something similar to:
  //
  //   WARNING: Still waiting for event to happen after 3 seconds...
  //   WARNING: Done waiting for event to happen after 5 seconds.
  //
  void WaitFor(const char* what);

  // Returns ZX_OK if |Signal|ed before |duration| elapses, or ZX_ERR_TIMED_OUT.
  zx_status_t TimedWait(zx::duration duration);

  // Returns ZX_OK if |Signal|ed before |deadline| is reached, or ZX_ERR_TIMED_OUT.
  zx_status_t WaitUntil(zx::time deadline);

  void Signal();

  void Reset();

 private:
  sync_completion_t sync_;
  zx::duration threshold_ = kDefaultThreshold;
  std::atomic<size_t> waiters_ = 0;
  std::atomic<bool> exceeded_ = false;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(SyncWait);
};

}  // namespace fuzzing

#endif  // SRC_SYS_FUZZING_COMMON_SYNC_WAIT_H_
