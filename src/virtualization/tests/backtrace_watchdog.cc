// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/tests/backtrace_watchdog.h"

#include <lib/syslog/cpp/macros.h>

#include <inspector/inspector.h>
#include <task-utils/walker.h>

namespace {

// Helper class to implement the TaskEnumerator for job walker.
class Enumerator : public TaskEnumerator {
 public:
  Enumerator() = default;
  ~Enumerator() = default;

  zx_status_t OnProcess(int depth, zx_handle_t process, zx_koid_t koid,
                        zx_koid_t parent_koid) override {
    inspector_print_debug_info_for_all_threads(stdout, process);
    return ZX_OK;
  }

  bool has_on_process() const override { return true; }
};

}  // namespace

zx_status_t BacktraceWatchdog::Start(zx::job job, zx::duration wait_time) {
  if (running_) {
    return ZX_ERR_ALREADY_EXISTS;
  }
  zx_status_t status = zx::event::create(0, &event_);
  if (status != ZX_OK) {
    return status;
  }
  job_ = std::move(job);
  timeout_ = zx::deadline_after(wait_time);
  // This completes the state mutation and so perform a barrier to ensure the spawned thread sees
  // the writes. Until the thread is joined we are therefore forbidden from state mutation.
  std::atomic_thread_fence(std::memory_order_seq_cst);
  thread_ = std::thread(&BacktraceWatchdog::WatchdogThread, this);
  running_ = true;
  return ZX_OK;
}

BacktraceWatchdog::~BacktraceWatchdog() { Stop(); }

void BacktraceWatchdog::Stop() {
  if (!running_) {
    return;
  }
  zx_status_t status = event_.signal(0, ZX_USER_SIGNAL_0);
  FX_CHECK(status == ZX_OK);
  thread_.join();
  // thread_ has been joined, so we can safely manipulate the state now.
  job_.reset();
  event_.reset();
  running_ = false;
}

int BacktraceWatchdog::WatchdogThread() {
  zx_signals_t pending;
  zx_status_t status = event_.wait_one(ZX_USER_SIGNAL_0, timeout_, &pending);
  if (status == ZX_ERR_TIMED_OUT) {
    Backtrace();
  }
  // Nothing to be done for any error status, just return.
  return 0;
}

void BacktraceWatchdog::Backtrace() {
  Enumerator enumerator;
  enumerator.WalkJobTree(job_.get());
}
