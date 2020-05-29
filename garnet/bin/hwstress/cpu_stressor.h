// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_
#define GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_

#include <zircon/compiler.h>

#include <atomic>
#include <thread>
#include <vector>

#include "profile_manager.h"

namespace hwstress {

// A StopIndicator is a light-weight class allowing one thread to indicate to
// one or more other threads when it should stop.
//
// Unlike an event, it does not support blocking: just polling of the |ShouldStop|
// method.
//
// StopIndicator is thread-safe.
class StopIndicator {
 public:
  StopIndicator() = default;

  // Indicate that other threads should stop.
  void Stop();

  // Determine if we should stop.
  inline bool ShouldStop() const {
    // We use a relaxed read to minimise overhead of polling "should_stop_".
    if (unlikely(should_stop_.load(std::memory_order_relaxed))) {
      // If we see it transition to "true", though, we want to perform an
      // aquire so that any other memory written by the thread that called
      // Stop() becomes visible.
      std::atomic_thread_fence(std::memory_order_acquire);
      return true;
    }

    return false;
  }

 private:
  std::atomic<bool> should_stop_ = false;
};

// A CpuStressor performs the given workload on multiple CPUs in the system,
// coordinating the creation and destruction of threads.
class CpuStressor {
 public:
  ~CpuStressor();

  // Create a CPU stressor that runs the given workload function.
  //
  // |workload| should loop until the given StopIndicator has its
  // |ShouldStop| method return true.
  CpuStressor(uint32_t threads, std::function<void(const StopIndicator&)> workload,
              ProfileManager* manager = nullptr);

  // Create a CPU stressor that calls the given workload function in
  // a tight loop.
  //
  // The given workload should perform a small chunk of work (roughly in
  // the range of 100 microseconds to 10 milliseconds) that exercises the
  // CPU.
  CpuStressor(uint32_t threads, std::function<void()> looping_workload,
              ProfileManager* manager = nullptr);

  // Start the workload. Must not already be started.
  void Start();

  // Stop the workload, blocking until all threads have completed.
  void Stop();

 private:
  // Disallow copy (and implicitly disallow move).
  CpuStressor(const CpuStressor&) = delete;
  CpuStressor& operator=(const CpuStressor&) = delete;

  uint32_t threads_;
  std::function<void(const StopIndicator&)> workload_;
  std::vector<std::unique_ptr<std::thread>> workers_;
  StopIndicator indicator_;
  ProfileManager* profile_manager_;  // Optional, owned elsewhere.
};

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_
