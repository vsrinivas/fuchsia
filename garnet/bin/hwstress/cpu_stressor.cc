// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_stressor.h"

#include <fuchsia/scheduler/cpp/fidl.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/zx/profile.h>
#include <lib/zx/status.h>
#include <zircon/assert.h>

#include <atomic>
#include <thread>
#include <unordered_map>
#include <utility>

#include <fbl/string_printf.h>

#include "cpu_workloads.h"
#include "profile_manager.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

void StopIndicator::Stop() { should_stop_.store(true, std::memory_order_release); }

CpuStressor::CpuStressor(uint32_t threads, std::function<void(const StopIndicator&)> workload,
                         ProfileManager* profile_manager)
    : threads_(threads), workload_(std::move(workload)), profile_manager_(profile_manager) {}

CpuStressor::CpuStressor(uint32_t threads, std::function<void()> looping_workload,
                         ProfileManager* profile_manager)
    : threads_(threads),
      workload_([f = std::move(looping_workload)](const StopIndicator& indicator) {
        do {
          f();
        } while (!indicator.ShouldStop());
      }),
      profile_manager_(profile_manager) {}

CpuStressor::~CpuStressor() { Stop(); }

void CpuStressor::Start() {
  ZX_ASSERT(workers_.empty());

  // Start the workers.
  for (uint32_t i = 0; i < threads_; i++) {
    auto worker = std::make_unique<std::thread>([this, workload = workload_, i]() mutable {
      // Set priority to low, and set affinity to CPU (i %  num_cpus).
      if (profile_manager_ != nullptr) {
        zx_status_t status =
            profile_manager_->SetThreadPriority(*zx::thread::self(), ZX_PRIORITY_LOW);
        ZX_ASSERT(status == ZX_OK);
        profile_manager_->SetThreadAffinity(*zx::thread::self(),
                                            1u << (i % zx_system_get_num_cpus()));
        ZX_ASSERT(status == ZX_OK);
      }

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
  for (auto& thread : workers_) {
    thread->join();
  }
  workers_.clear();
}

}  // namespace hwstress
