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
  for (auto& thread : workers_) {
    thread->join();
  }
  workers_.clear();
}

namespace {

void RunWorkload(StatusLine* status, TemperatureSensor* sensor, const Workload& workload,
                 uint32_t num_cpus, zx::duration duration) {
  // Start a workload.
  CpuStressor stressor{num_cpus, workload.work};
  stressor.Start();

  // Update the status line until the test is finished.
  zx::time start_time = zx::clock::get_monotonic();
  zx::time end_time = start_time + duration;
  std::optional<double> temperature;
  while (zx::clock::get_monotonic() < end_time) {
    // Sleep for 250ms or the finish time, whichever is sooner.
    zx::time next_update = zx::deadline_after(zx::msec(250));
    zx::nanosleep(std::min(end_time, next_update));

    // Update the status line.
    temperature = sensor->ReadCelcius();
    zx::duration time_running = zx::clock::get_monotonic() - start_time;
    status->Set("  %02ld:%02ld:%02ld || Current test: %s || System temperature: %s",
                time_running.to_hours(), time_running.to_mins() % 60, time_running.to_secs() % 60,
                workload.name.c_str(), TemperatureToString(temperature).c_str());
  }
  stressor.Stop();

  // Log final temperature
  status->Set("");
  status->Log("* Workload %s complete : final temp: %s\n", workload.name.c_str(),
              TemperatureToString(temperature).c_str());
}

}  // namespace

void StressCpu(zx::duration duration, TemperatureSensor* temperature_sensor) {
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

  // Get workloads.
  std::vector<Workload> workloads = GetWorkloads();

  // Determine the time per test, and how many times we should loop through the different workloads.
  zx::duration time_per_test;
  uint64_t iterations;
  if (duration == zx::duration::infinite()) {
    // If we are running forever, just use 5 minutes for each.
    time_per_test = zx::sec(300);
    iterations = UINT64_MAX;
  } else {
    time_per_test = duration / workloads.size();
    iterations = 1;
  }

  // Run the workloads.
  for (uint64_t iteration = 0; iteration < iterations; iteration++) {
    for (const auto& workload : workloads) {
      RunWorkload(&status, temperature_sensor, workload, num_cpus, time_per_test);
    }
  }

  status.Log("Complete.\n");
}

}  // namespace hwstress
