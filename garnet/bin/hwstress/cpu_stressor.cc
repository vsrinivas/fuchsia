// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_stressor.h"

#include <zircon/assert.h>

#include <atomic>
#include <thread>
#include <utility>

#include "cpu_workloads.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

void StopIndicator::Stop() { should_stop_.store(true, std::memory_order_release); }

CpuStressor::CpuStressor(uint32_t threads, std::function<void(const StopIndicator &)> workload)
    : threads_(threads), workload_(std::move(workload)) {}

CpuStressor::CpuStressor(uint32_t threads, std::function<void()> looping_workload)
    : threads_(threads),
      workload_([f = std::move(looping_workload)](const StopIndicator &indicator) {
        do {
          f();
        } while (!indicator.ShouldStop());
      }) {}

CpuStressor::~CpuStressor() { Stop(); }

void CpuStressor::Start() {
  ZX_ASSERT(workers_.empty());

  // Start the workers.
  for (uint32_t i = 0; i < threads_; i++) {
    auto worker = std::make_unique<std::thread>([this, workload = workload_]() mutable {
      // Run the workload.
      workload(indicator_);
      // Ensure the function didn't return while ShouldStop() was still false.
      ZX_ASSERT(indicator_.ShouldStop());
    });
    workers_.push_back(std::move(worker));
  }
}

void CpuStressor::Stop() {
  indicator_.Stop();
  for (auto &thread : workers_) {
    thread->join();
  }
  workers_.clear();
}

}  // namespace hwstress
