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

#include "cpu_workloads.h"
#include "lib/zx/time.h"
#include "profile_manager.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

zx::duration GetCurrentThreadCpuTime() {
  zx_info_thread_stats stats = {};
  zx::thread::self()->get_info(ZX_INFO_THREAD_STATS, &stats, sizeof(stats), nullptr, nullptr);
  return zx::duration(stats.total_runtime);
}

zx::duration RequiredSleepForTargetUtilization(zx::duration cpu_time, zx::duration wall_time,
                                               double utilization) {
  double sleep_time = (DurationToSecs(cpu_time) / utilization) - DurationToSecs(wall_time);

  // If we have been running under utilization, there is no need to sleep.
  if (sleep_time <= 0) {
    return zx::sec(0);
  }

  // Otherwise, sleep for for an amount of time that will make our utilization
  // drop below the target.
  return SecsToDuration(sleep_time);
}

void WorkIndicator::MaybeSleep() {
  // Determine how long we need to sleep to reach "utilization", based on
  // consumed CPU time and wall time.
  zx::time now = zx::clock::get_monotonic();
  zx::duration sleep_time =
      RequiredSleepForTargetUtilization(/*cpu_time=*/GetCurrentThreadCpuTime(),
                                        /*wall_time=*/(now - start_time_), utilization_);

  // Sleep if we need to decrease our utilization.
  //
  // We sleep a tad longer than what we strictly need to. If we didn't, we would
  // only be able to perform 1 more iteration of the workload before needing to
  // sleep again.
  //
  // Sleeping a tad longer drops our utilization below the target value, and
  // hence allows us to run longer after we wake up. The goal here is to reduce
  // the number of sleeps (and hence context switches) overall, so we spend more
  // time in the workload and less time in the kernel.
  if (sleep_time > zx::sec(0)) {
    zx::nanosleep(now + sleep_time + zx::msec(50));
  }
}

void StopIndicator::Stop() { should_stop_.store(true, std::memory_order_release); }

CpuStressor::CpuStressor(std::vector<uint32_t> cores_to_test,
                         std::function<void(WorkIndicator)> workload, double utilization,
                         ProfileManager* profile_manager)
    : cores_to_test_(std::move(cores_to_test)),
      workload_(std::move(workload)),
      utilization_(utilization),
      profile_manager_(profile_manager) {}

CpuStressor::CpuStressor(std::vector<uint32_t> cores_to_test,
                         std::function<void()> looping_workload, double utilization,
                         ProfileManager* profile_manager)
    : cores_to_test_(std::move(cores_to_test)),
      workload_([f = std::move(looping_workload)](WorkIndicator indicator) {
        do {
          f();
        } while (!indicator.ShouldStop());
      }),
      utilization_(utilization),
      profile_manager_(profile_manager) {}

CpuStressor::~CpuStressor() { Stop(); }

void CpuStressor::Start() {
  ZX_ASSERT(workers_.empty());

  // Start the workers.
  for (uint32_t core : cores_to_test_) {
    auto worker = std::make_unique<std::thread>([this, workload = workload_, core]() mutable {
      // Set priority to low, and set affinity to CPU (core % num_cpus).
      if (profile_manager_ != nullptr) {
        zx_status_t status =
            profile_manager_->SetThreadPriority(*zx::thread::self(), ZX_PRIORITY_LOW);
        ZX_ASSERT(status == ZX_OK);
        profile_manager_->SetThreadAffinity(*zx::thread::self(),
                                            1u << (core % zx_system_get_num_cpus()));
        ZX_ASSERT(status == ZX_OK);
      }

      // Run the workload.
      workload(WorkIndicator(indicator_, utilization_));

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
