// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_stress.h"

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

#include "args.h"
#include "cpu_stressor.h"
#include "cpu_workloads.h"
#include "status.h"
#include "temperature_sensor.h"
#include "util.h"

namespace hwstress {

namespace {

void RunWorkload(StatusLine* status, ProfileManager* profile_manager, TemperatureSensor* sensor,
                 const CpuWorkload& workload, uint32_t num_cpus, zx::duration duration,
                 double utilization) {
  // Start a workload.
  CpuStressor stressor{num_cpus, workload.work, utilization, profile_manager};
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
  status->Log("* CpuWorkload %s complete after %0.2fs: final temp: %s\n", workload.name.c_str(),
              DurationToSecs(duration), TemperatureToString(temperature).c_str());
}

// Get a list of workloads to run.
std::optional<std::vector<CpuWorkload>> GetWorkloads(const CommandLineArgs& args) {
  // Fetch all workloads.
  std::vector<CpuWorkload> workloads = GetCpuWorkloads();

  // If no specific workload was used, run them all.
  if (args.cpu_workload.empty()) {
    return workloads;
  }

  // Otherwise, find the named workload.
  for (const CpuWorkload& workload : workloads) {
    if (workload.name == args.cpu_workload) {
      return std::vector<CpuWorkload>{workload};
    }
  }

  // Invalid workload.
  fprintf(stderr,
          "Invalid workload name: '%s'.\n\n"
          "Valid workload names are:\n",
          args.cpu_workload.c_str());
  for (const CpuWorkload& workload : workloads) {
    fprintf(stderr, "  %s\n", workload.name.c_str());
  }

  return std::nullopt;
}

}  // namespace

bool StressCpu(StatusLine* status, const CommandLineArgs& args, zx::duration duration,
               TemperatureSensor* temperature_sensor) {
  // Calculate finish time.
  zx::time start_time = zx::clock::get_monotonic();
  zx::time finish_time = start_time + duration;

  // Get number of CPUs.
  uint32_t num_cpus = zx_system_get_num_cpus();
  status->Log("Detected %d CPU(s) in the system.\n", num_cpus);

  // Create a profile manager.
  std::unique_ptr<ProfileManager> profile_manager = ProfileManager::CreateFromEnvironment();
  if (profile_manager == nullptr) {
    status->Log("Error: could not create profile manager.");
    return false;
  }

  // Print start banner.
  if (finish_time == zx::time::infinite()) {
    status->Log("Exercising CPU until stopped...\n");
  } else {
    status->Log("Exercising CPU for %0.2f seconds...\n", DurationToSecs(duration));
  }

  // Get workloads.
  std::optional<std::vector<CpuWorkload>> maybe_workloads = GetWorkloads(args);
  if (!maybe_workloads.has_value()) {
    return false;
  }
  std::vector<CpuWorkload> workloads = std::move(maybe_workloads).value();

  // Get initial time per test.
  //
  // Our strategy is to run through the tests multiple times, doubling the
  // runtime each time. This allows us to catch obvious faults detected by
  // a particular test quickly, while later on moving to a "burn in" mode. It
  // also has the added benefit that if our process is terminated at an
  // arbitrary point, no one test will have run for more than twice as long as
  // any other test.
  //
  // When the user has passed in a fixed test duration, we additionally want all
  // tests to have an equal run time. We thus choose an initial test time such
  // that after runtime doubling is applied will cause the test end time to
  // coincide with the end of a full round of tests.
  zx::duration time_per_test;
  if (finish_time == zx::time::infinite()) {
    time_per_test = zx::msec(100);
  } else {
    // After running through K tests N times, doubling the test time after
    // each, and starting with an initial test time of D, we will have run for:
    //
    //    D * K * (2**(N + 1) - 1)
    //
    // we select the largest such D such that:
    //
    //   1. Above equation evenly divides the total desired test duration; and
    //   2. "D" is no larger than 100 msec.
    uint64_t n = 1;
    do {
      time_per_test = duration / (workloads.size() * ((1ul << n) - 1));
      n++;
    } while (time_per_test > zx::msec(100) && n < 63);
  }

  // Run the workloads.
  uint64_t iteration = 1;
  do {
    status->Log("Iteration %ld: %0.2fs per test.", iteration++, DurationToSecs(time_per_test));
    for (const auto& workload : workloads) {
      RunWorkload(status, profile_manager.get(), temperature_sensor, workload, num_cpus,
                  time_per_test, /*utilization=*/(args.utilization_percent / 100.0));
    }
    time_per_test *= 2;
  } while (zx::clock::get_monotonic() < finish_time);

  status->Log("Complete.\n");
  return true;
}

}  // namespace hwstress
