// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_
#define GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_

#include <zircon/compiler.h>

#include <atomic>
#include <thread>
#include <vector>

#include "lib/zx/time.h"
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

// A WorkIterator provides a way for workloads to determine how long
// they should carry out work for.
class WorkIndicator {
 public:
  explicit WorkIndicator(StopIndicator& indicator, double utilization)
      : utilization_(utilization), start_time_(zx::clock::get_monotonic()), indicator_(indicator) {}

  // Determine if we should stop, and possibly sleep to reduce CPU utilization.
  inline bool ShouldStop() {
    // Fast path: if we desire 100% utilization, don't do any further analysis.
    if (utilization_ >= 1) {
      return indicator_.ShouldStop();
    }

    iterations_++;

    // Determine if it is time to stop.
    if (indicator_.ShouldStop()) {
      return true;
    }

    MaybeSleep();
    return false;
  }

 private:
  // Possibly sleep for a short period of time to ensure that the
  // current thread's runtime doesn't exceed |utilization_| of the
  // wall time.
  void MaybeSleep();

  double utilization_;
  zx::time start_time_;
  StopIndicator& indicator_;
  uint64_t iterations_ = 0;
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
  //
  // |utilization| should be a value between 0.0 and 1.0 indicating the
  // fraction of CPU that should be used in the long run.
  CpuStressor(std::vector<uint32_t> cores_to_test, std::function<void(WorkIndicator)> workload,
              double utilization = 1.0, ProfileManager* manager = nullptr);

  // Create a CPU stressor that calls the given workload function in
  // a tight loop.
  //
  // The given workload should perform a small chunk of work (roughly in
  // the range of 100 microseconds to 10 milliseconds) that exercises the
  // CPU.
  //
  // |utilization| should be a value between 0.0 and 1.0 indicating the
  // fraction of CPU that should be used in the long run.
  CpuStressor(std::vector<uint32_t> cores_to_test, std::function<void()> looping_workload,
              double utilization = 1.0, ProfileManager* manager = nullptr);

  // Start the workload. Must not already be started.
  void Start();

  // Stop the workload, blocking until all threads have completed.
  void Stop();

 private:
  // Disallow copy (and implicitly disallow move).
  CpuStressor(const CpuStressor&) = delete;
  CpuStressor& operator=(const CpuStressor&) = delete;

  std::vector<uint32_t> cores_to_test_;
  std::function<void(WorkIndicator)> workload_;
  std::vector<std::unique_ptr<std::thread>> workers_;
  StopIndicator indicator_;
  double utilization_;               // value in (0.0, 1.0] indicating the fraction of CPU to use.
  ProfileManager* profile_manager_;  // Optional, owned elsewhere.
};

// Given a thread has had |cpu_time| CPU time and |wall_time| wall time, how long must we sleep to
// achieve a utilization of |utilization|?
zx::duration RequiredSleepForTargetUtilization(zx::duration cpu_time, zx::duration wall_time,
                                               double utilization);

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_CPU_STRESSOR_H_
