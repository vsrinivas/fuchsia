// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "abs_clock/clock.h"

#include <lib/sync/completion.h>
#include <lib/zx/time.h>
#include <zircon/errors.h>
#include <zircon/time.h>

#include <string>
#include <thread>

#include <zxtest/zxtest.h>

namespace abs_clock {
namespace {

// Introduce a small pause in the program, giving other threads a chance
// to schedule.
//
// The idea here is that Pause()'s will expose races in buggy
// code, and not be required for tests to correctly pass.
void Pause() { zx::nanosleep(zx::deadline_after(zx::msec(10))); }

TEST(RealClock, GetTime) {
  Clock* clock = RealClock::Get();

  // Fetch the current time.
  zx::time a = clock->Now();

  // Keep fetching the time until we see it change.
  //
  // Succesive calls to clock->Now() might return the same value, but we should never
  // see it go backwards.
  zx::time b;
  do {
    b = clock->Now();
    EXPECT_GE(b, a);
  } while (b <= a);
}

TEST(RealClock, Sleep) {
  // Sleep for a short time.
  zx::time before = zx::time(zx_clock_get_monotonic());
  RealClock::Get()->SleepUntil(zx::deadline_after(zx::msec(10)));
  zx::time after = zx::time(zx_clock_get_monotonic());

  // Difference in times should be >= 10 msec.
  EXPECT_GE(after - before, zx::msec(10));
}

TEST(FakeClock, GetTime) {
  FakeClock clock{zx::time(123)};
  EXPECT_EQ(clock.Now(), zx::time(123));

  clock.AdvanceTime(zx::sec(100));
  EXPECT_EQ(clock.Now(), zx::time(123) + zx::sec(100));
}

TEST(FakeClock, SleepInPast) {
  FakeClock clock{zx::time(1000)};
  clock.SleepUntil(zx::time(500));
}

TEST(FakeClock, SleepUntil) {
  constexpr zx::time kStartTime = zx::time{123};
  FakeClock clock{kStartTime};
  sync_completion_t thread_awake;

  std::thread sleeper([kStartTime, &clock, &thread_awake]() {
    clock.SleepUntil(kStartTime + zx::sec(1));
    sync_completion_signal(&thread_awake);
  });

  // Give our helper thread a chance to sleep, and ensure it didn't wake up.
  Pause();
  EXPECT_FALSE(sync_completion_signaled(&thread_awake));

  // Advance time to before the wake up time. Ensure the thread still didn't
  // wake up.
  clock.AdvanceTime(zx::msec(999));
  Pause();
  EXPECT_FALSE(sync_completion_signaled(&thread_awake));

  // Advance time to past the wake up time. The thread should wake up.
  clock.AdvanceTime(zx::msec(2));
  sync_completion_wait(&thread_awake, ZX_TIME_INFINITE);

  sleeper.join();
}

TEST(FakeClock, SleepUntilPreciseTime) {
  constexpr zx::time kStartTime = zx::time{123};
  FakeClock clock{kStartTime};
  sync_completion_t thread_awake;

  std::thread sleeper([kStartTime, &clock, &thread_awake]() {
    clock.SleepUntil(kStartTime + zx::sec(1));
    sync_completion_signal(&thread_awake);
  });

  // Advance time to precisely the wake up time. The thread should wake up.
  clock.AdvanceTime(zx::sec(1));
  sync_completion_wait(&thread_awake, ZX_TIME_INFINITE);

  sleeper.join();
}

TEST(FakeClock, MultipleThreadsSleeping) {
  constexpr zx::time kStartTime = zx::time{123};
  FakeClock clock{};
  sync_completion_t thread1_awake;
  sync_completion_t thread2_awake;

  // Create two threads, one sleeping for 1 second, the second for 2 seconds.
  std::thread sleeper1([kStartTime, &clock, &thread1_awake]() {
    clock.SleepUntil(kStartTime + zx::sec(1));
    sync_completion_signal(&thread1_awake);
  });

  std::thread sleeper2([kStartTime, &clock, &thread2_awake]() {
    clock.SleepUntil(kStartTime + zx::sec(2));
    sync_completion_signal(&thread2_awake);
  });
  Pause();

  // Advance time so the first thread wakes up, but not the second.
  clock.AdvanceTime(zx::msec(1'500));
  sync_completion_wait(&thread1_awake, ZX_TIME_INFINITE);
  EXPECT_FALSE(sync_completion_signaled(&thread2_awake));

  // Wake up the second thread.
  clock.AdvanceTime(zx::sec(1));
  sync_completion_wait(&thread2_awake, ZX_TIME_INFINITE);

  sleeper1.join();
  sleeper2.join();
}

}  // namespace
}  // namespace abs_clock
