// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_TESTS_BACKTRACE_WATCHDOG_H_
#define SRC_VIRTUALIZATION_TESTS_BACKTRACE_WATCHDOG_H_

#include <lib/zx/event.h>
#include <lib/zx/job.h>
#include <lib/zx/time.h>

#include <thread>

// BacktraceWatchdog is a one-shot watchdog that backtraces all the threads in a job if the timeout
// is reached prior to the watchdog being stopped. The watchdog spawns its own thread and does not
// rely on a thread being available on any dispatcher to trigger the timeout.
class BacktraceWatchdog {
 public:
  BacktraceWatchdog() = default;
  ~BacktraceWatchdog();

  // Start the watchdog with the given job and timeout.
  zx_status_t Start(zx::job job, zx::duration wait_time);

  // Stop the watchdog early. This happens implicitly on destruction.
  void Stop();

 private:
  int WatchdogThread();
  void Backtrace();

  bool running_ = false;
  zx::time timeout_;
  std::thread thread_;
  zx::job job_;
  zx::event event_;
};

#endif  // SRC_VIRTUALIZATION_TESTS_BACKTRACE_WATCHDOG_H_
