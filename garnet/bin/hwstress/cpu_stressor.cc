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
  for (auto &thread : workers_) {
    thread->join();
  }
  workers_.clear();
}

}  // namespace hwstress
