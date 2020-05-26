// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_HWSTRESS_CPU_H_
#define GARNET_BIN_HWSTRESS_CPU_H_

#include <lib/zx/time.h>

#include <atomic>
#include <thread>
#include <vector>

namespace hwstress {

// Start a stress test.
void StressCpu(zx::duration duration_seconds);

//
// The following are exposed for testing.
//

// A CpuStressor performs the given workload on multiple CPUs in the system.
//
// The given workload should perfom a small chunk of work (roughly in
// the range of 100 microsconds to 10 milliseconds) that exercises the
// CPU. The function will be called repeatedly in a tight loop on
// multiple threads in the system until the class is told to |Stop|.
class CpuStressor {
 public:
  CpuStressor(uint32_t threads, std::function<void()> workload);
  ~CpuStressor();

  // Start the workload. Must not already be started.
  void Start();

  // Stop the workload, blocking until all threads have completed.
  void Stop();

 private:
  // Disallow copy (and implicitly disallow move).
  CpuStressor(const CpuStressor&) = delete;
  CpuStressor& operator=(const CpuStressor&) = delete;

  uint32_t threads_;
  std::function<void()> workload_;
  std::vector<std::unique_ptr<std::thread>> workers_;
  std::atomic_bool should_stop_;
};

}  // namespace hwstress

#endif  // GARNET_BIN_HWSTRESS_CPU_H_
