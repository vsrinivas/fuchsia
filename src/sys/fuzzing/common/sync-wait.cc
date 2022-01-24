// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/sync-wait.h"

#include <lib/backtrace-request/backtrace-request.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace fuzzing {

using Waiter = fit::function<zx_status_t(zx::time)>;

zx_duration_t gThreshold = ZX_SEC(30);

// Waiter functions

zx_status_t WaitFor(const char* what, Waiter* waiter) {
  if (gThreshold <= 0) {
    return (*waiter)(zx::time::infinite());
  }
  zx::duration threshold(gThreshold);
  auto deadline = zx::deadline_after(threshold);
  auto status = (*waiter)(deadline);
  if (status != ZX_ERR_TIMED_OUT) {
    return status;
  }
  auto elapsed = threshold / zx::sec(1);
  FX_LOGS(WARNING) << "Still waiting for " << what << " after " << elapsed << " seconds...";
  if (gThreshold >= ZX_SEC(30)) {
    backtrace_request();
  }
  auto start = zx::clock::get_monotonic() - threshold;
  status = (*waiter)(zx::time::infinite());
  elapsed = (zx::clock::get_monotonic() - start) / zx::sec(1);
  FX_LOGS(WARNING) << "Done waiting for " << what << " after " << elapsed << " seconds.";
  return status;
}

zx_status_t PollFor(const char* what, Waiter* waiter, zx::duration interval) {
  Waiter wrapper = [waiter, interval](zx::time deadline) {
    while (true) {
      auto step = zx::deadline_after(interval);
      auto status = (*waiter)(step);
      if (status != ZX_ERR_TIMED_OUT) {
        return status;
      }
      if (deadline < zx::clock::get_monotonic()) {
        return ZX_ERR_TIMED_OUT;
      }
    }
  };
  return WaitFor(what, &wrapper);
}

void SetThreshold(zx::duration threshold) { gThreshold = threshold.get(); }

void ResetThreshold() { gThreshold = ZX_SEC(30); }

void DisableSlowWaitLogging() { gThreshold = 0; }

// SyncWait methods

SyncWait::SyncWait()
    : waiter_([this](zx::time deadline) {
        ++waiters_;
        auto status = sync_completion_wait_deadline(&sync_, deadline.get());
        auto waiters = waiters_.fetch_sub(1);
        FX_DCHECK(waiters != 0);
        return status;
      }) {}

SyncWait::~SyncWait() { FX_DCHECK(waiters_ == 0); }

void SyncWait::WaitFor(const char* what) { ::fuzzing::WaitFor(what, &waiter_); }

zx_status_t SyncWait::TimedWait(zx::duration duration) {
  return waiter_(zx::deadline_after(duration));
}

zx_status_t SyncWait::WaitUntil(zx::time deadline) { return waiter_(deadline); }

void SyncWait::Signal() { sync_completion_signal(&sync_); }

void SyncWait::Reset() { sync_completion_reset(&sync_); }

}  // namespace fuzzing
