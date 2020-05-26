// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu.h"

#include <lib/zx/clock.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <unistd.h>
#include <zircon/assert.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <string>
#include <thread>
#include <utility>

#include <fbl/string_printf.h>

#include "status.h"
#include "util.h"

namespace hwstress {

CpuStressor::CpuStressor(uint32_t threads, std::function<void()> workload)
    : threads_(threads), workload_(std::move(workload)) {}

CpuStressor::~CpuStressor() { Stop(); }

void CpuStressor::Start() {
  ZX_ASSERT(workers_.empty());
  should_stop_.store(false);

  // Start the workers.
  for (uint32_t i = 0; i < threads_; i++) {
    auto worker = std::make_unique<std::thread>([this, workload = workload_]() mutable {
      while (!should_stop_.load(std::memory_order_relaxed)) {
        workload();
      }
    });
    workers_.push_back(std::move(worker));
  }
}

void CpuStressor::Stop() {
  should_stop_.store(true);
  for (auto& thread : workers_) {
    thread->join();
  }
  workers_.clear();
}

void StressCpu(zx::duration duration) {
  StatusLine status;

  // Calculate finish time.
  zx::time start_time = zx::clock::get_monotonic();
  zx::time finish_time = start_time + duration;

  // Get number of CPUs.
  uint32_t num_cpus = zx_system_get_num_cpus();
  status.Log("Detected %d CPU(s) in the system.\n", num_cpus);

  // Print start banner.
  if (finish_time == zx::time::infinite()) {
    status.Log("Exercising CPU until stopped...\n");
  } else {
    status.Log("Exercising CPU for %0.2f seconds...\n", DurationToSecs(duration));
  }

  // Start a workload.
  CpuStressor stressor{num_cpus, []() { /* do nothing */ }};
  stressor.Start();

  // Run the loop, updating the status line as we go.
  while (zx::clock::get_monotonic() < finish_time) {
    // Sleep for 250ms or the finish time, whichever is sooner.
    zx::time next_update = zx::deadline_after(zx::msec(250));
    zx::nanosleep(std::min(finish_time, next_update));

    // Update the status line.
    zx::duration time_running = zx::clock::get_monotonic() - start_time;
    status.Set("%02ld:%02ld:%02ld", time_running.to_hours(), time_running.to_mins() % 60,
               time_running.to_secs() % 60);
  }

  status.Set("");
  status.Log("Complete.\n");
  stressor.Stop();
}

}  // namespace hwstress
