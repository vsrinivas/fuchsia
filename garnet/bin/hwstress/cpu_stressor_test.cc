// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu_stressor.h"

#include <lib/zx/time.h>
#include <zircon/compiler.h>
#include <zircon/status.h>

#include <atomic>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(CpuStressor, TrivialStartStop) {
  CpuStressor stressor{{1}, []() { /* do nothing */ }};
  stressor.Start();
  stressor.Stop();
}

TEST(CpuStressor, EnsureFunctionRunsAndStops) {
  std::atomic<uint32_t> val;
  CpuStressor stressor{{1}, [&]() { val.fetch_add(1); }};
  stressor.Start();

  // Ensure we see the counter change a few times.
  uint32_t last_val = val.load();

  for (int i = 0; i < 3; i++) {
    // Keep reading "val" until we see it change, sleeping a (exponentially
    // increasing) amount of time after each unchanged read.
    zx::duration sleep_time = zx::nsec(1);
    while (val == last_val) {
      zx::nanosleep(zx::deadline_after(sleep_time));
      sleep_time *= 2;
    }
    last_val = val;
  }

  stressor.Stop();

  // We shouldn't see the counter change any more.
  uint32_t final_val = val.load();
  zx::nanosleep(zx::deadline_after(zx::msec(1)));
  EXPECT_EQ(final_val, val.load());
}

TEST(CpuStressor, MultipleThreads) {
  const int kNumThreads = 10;
  std::atomic<uint32_t> seen_threads;
  std::vector<uint32_t> cores;
  for (uint32_t i = 0; i < kNumThreads; i++) {
    cores.push_back(i);
  }

  // Each thread increments the "seen_threads" counter once.
  CpuStressor stressor{cores, [&seen_threads, added = false]() mutable {
                         if (!added) {
                           seen_threads.fetch_add(1);
                         }
                         added = true;
                       }};
  stressor.Start();

  // Wait until we've seen all 10 threads.
  zx::duration sleep_time = zx::nsec(1);
  while (seen_threads.load() < kNumThreads) {
    zx::nanosleep(zx::deadline_after(sleep_time));
    sleep_time *= 2;
  }

  stressor.Stop();
}

TEST(CpuStressor, RequiredSleepForTargetUtilization) {
  // Used 1 second of CPU time in 1 second of wall time. Need to sleep 1 second to reach 50%
  // utilization.
  EXPECT_EQ(RequiredSleepForTargetUtilization(zx::sec(1), zx::sec(1), 0.5), zx::sec(1));

  // Used 1 second of CPU time in 10 seconds of wall time. Don't need to sleep to reach 50%
  // utilization.
  EXPECT_EQ(RequiredSleepForTargetUtilization(zx::sec(1), zx::sec(10), 0.5), zx::sec(0));

  // 1 hour + 1 second of CPU time over 2 hours. Need to sleep for 2 seconds.
  constexpr zx::duration kHour = zx::sec(3600);
  EXPECT_EQ(RequiredSleepForTargetUtilization(kHour + zx::sec(1), kHour * 2, 0.5), zx::sec(2));

  // 1 second CPU time over 1 second of wall time at 10% utilization. Need to sleep 9 seconds.
  EXPECT_EQ(RequiredSleepForTargetUtilization(zx::sec(1), zx::sec(1), 0.1), zx::sec(9));
}

}  // namespace
}  // namespace hwstress
