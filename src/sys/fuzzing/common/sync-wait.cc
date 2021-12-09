// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/sys/fuzzing/common/sync-wait.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/zx/clock.h>

namespace fuzzing {

SyncWait::~SyncWait() { FX_DCHECK(waiters_ == 0); }

void SyncWait::WaitFor(const char* what) {
  if (threshold_ <= zx::duration(0)) {
    WaitUntil(zx::time::infinite());
    return;
  }
  if (TimedWait(threshold_) == ZX_OK) {
    return;
  }
  exceeded_ = true;
  auto elapsed = threshold_ / zx::sec(1);
  FX_LOGS(WARNING) << "Still waiting for " << what << " after " << elapsed << " seconds...";
  auto start = zx::clock::get_monotonic() - threshold_;
  WaitUntil(zx::time::infinite());
  elapsed = (zx::clock::get_monotonic() - start) / zx::sec(1);
  FX_LOGS(WARNING) << "Done waiting for " << what << " after " << elapsed << " seconds.";
}

zx_status_t SyncWait::TimedWait(zx::duration duration) {
  ++waiters_;
  auto status = sync_completion_wait(&sync_, duration.get());
  auto waiters = waiters_.fetch_sub(1);
  FX_DCHECK(waiters != 0);
  return status;
}

zx_status_t SyncWait::WaitUntil(zx::time deadline) {
  ++waiters_;
  auto status = sync_completion_wait_deadline(&sync_, deadline.get());
  auto waiters = waiters_.fetch_sub(1);
  FX_DCHECK(waiters != 0);
  return status;
}

void SyncWait::Signal() { sync_completion_signal(&sync_); }

void SyncWait::Reset() {
  sync_completion_reset(&sync_);
  exceeded_ = false;
}

}  // namespace fuzzing
