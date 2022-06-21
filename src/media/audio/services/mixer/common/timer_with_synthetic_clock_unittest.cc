// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/common/timer_with_synthetic_clock.h"

#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ContainerEq;

namespace media_audio {

TEST(TimerWithSyntheticClockTest, Event) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(0));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer.SetEventBit();
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time::infinite());
  EXPECT_TRUE(timer.CurrentState().event_set);
  EXPECT_FALSE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(0));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, Shutdown) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(0));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer.SetShutdownBit();
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time::infinite());
  EXPECT_FALSE(timer.CurrentState().event_set);
  EXPECT_TRUE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(0));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, Timer) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(20));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time(10));
  EXPECT_FALSE(timer.CurrentState().event_set);
  EXPECT_FALSE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, TimerAdvanceToSameTime) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time(10));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(0));

    reason = timer.SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(10));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  // With a pending event, advancing to the same time should wake the timer.
  timer.SetEventBit();
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(0));

  // Without a pending event, advancing to the same time should not wake the timer.
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(0));

  // Advancing forward, so wake the timer.
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(10));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, TimerAndEvent) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(10));

    // The event bit should be cleared by the prior SleepUntil, so only the timer should fire.
    reason = timer.SleepUntil(zx::time(20));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(20));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer.SetEventBit();
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time(10));
  EXPECT_TRUE(timer.CurrentState().event_set);
  EXPECT_FALSE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(10));
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time(20));
  EXPECT_FALSE(timer.CurrentState().event_set);
  EXPECT_FALSE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, TimerAndShutdown) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(10));

    // The shutdown bit should persist.
    reason = timer.SleepUntil(zx::time(20));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(20));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer.SetShutdownBit();
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time(10));
  EXPECT_FALSE(timer.CurrentState().event_set);
  EXPECT_TRUE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(10));
  timer.WaitUntilSleeping();
  EXPECT_EQ(timer.CurrentState().deadline, zx::time(20));
  EXPECT_FALSE(timer.CurrentState().event_set);
  EXPECT_TRUE(timer.CurrentState().shutdown_set);

  timer.WakeAndAdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(TimerWithSyntheticClockTest, Advance) {
  TimerWithSyntheticClock timer(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer.SleepUntil(zx::time(25));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(20));

    reason = timer.SleepUntil(zx::time(25));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer.now(), zx::time(30));
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  // Nothing yet.
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(10));

  // Event fires.
  timer.SetEventBit();
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(20));

  // Timer fires.
  timer.WaitUntilSleeping();
  timer.WakeAndAdvanceTo(zx::time(30));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

}  // namespace media_audio
