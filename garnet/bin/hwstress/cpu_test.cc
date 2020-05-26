// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cpu.h"

#include <lib/zx/thread.h>
#include <lib/zx/time.h>
#include <stdio.h>
#include <zircon/compiler.h>
#include <zircon/status.h>
#include <zircon/syscalls.h>

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_set>

#include <gtest/gtest.h>

namespace hwstress {
namespace {

TEST(Cpu, TrivialStartStop) {
  CpuStressor stressor{1, []() { /* do nothing */ }};
  stressor.Start();
  stressor.Stop();
}

TEST(Cpu, EnsureFunctionRunsAndStops) {
  std::atomic<uint32_t> val;
  CpuStressor stressor{1, [&]() { val.fetch_add(1); }};
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

TEST(Cpu, MultipleThreads) {
  const int kNumThreads = 10;
  std::atomic<uint32_t> seen_threads;

  // Each thread increments the "seen_threads" counter once.
  CpuStressor stressor{kNumThreads, [&seen_threads, added = false]() mutable {
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

TEST(Cpu, StressCpu) {
  // Exercise the main StressCpu for a tiny amount of time.
  StressCpu(zx::msec(1));
}

}  // namespace
}  // namespace hwstress
