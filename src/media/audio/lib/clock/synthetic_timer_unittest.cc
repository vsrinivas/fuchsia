// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_timer.h"

#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::ContainerEq;

namespace media_audio {

TEST(SyntheticTimerTest, Event) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));
    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetEventBit();
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time::infinite());
  EXPECT_TRUE(timer->CurrentState().event_set);
  EXPECT_FALSE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(0));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticTimerTest, Shutdown) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));
    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetShutdownBit();
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time::infinite());
  EXPECT_FALSE(timer->CurrentState().event_set);
  EXPECT_TRUE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(0));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);

  // This should return immediately.
  timer->WaitUntilSleepingOrStopped();
}

TEST(SyntheticTimerTest, Timer) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(20));
    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time(10));
  EXPECT_FALSE(timer->CurrentState().event_set);
  EXPECT_FALSE(timer->CurrentState().shutdown_set);
  EXPECT_FALSE(timer->CurrentState().stopped);

  timer->AdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
  EXPECT_TRUE(timer->CurrentState().stopped);
}

TEST(SyntheticTimerTest, TimerAdvanceToSameTime) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time(10));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));

    reason = timer->SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(10));

    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  // With a pending event, advancing to the same time should wake the timer->
  timer->SetEventBit();
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(0));

  // Without a pending event, advancing to the same time should not wake the timer->
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(0));

  // Advancing forward, so wake the timer->
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(10));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticTimerTest, TimerAndEvent) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(10));

    // The event bit should be cleared by the prior SleepUntil, so only the timer should fire.
    reason = timer->SleepUntil(zx::time(20));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(20));

    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetEventBit();
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time(10));
  EXPECT_TRUE(timer->CurrentState().event_set);
  EXPECT_FALSE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(10));
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time(20));
  EXPECT_FALSE(timer->CurrentState().event_set);
  EXPECT_FALSE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticTimerTest, TimerAndShutdown) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(10));

    // The shutdown bit should persist.
    reason = timer->SleepUntil(zx::time(20));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(20));

    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetShutdownBit();
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time(10));
  EXPECT_FALSE(timer->CurrentState().event_set);
  EXPECT_TRUE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(10));
  timer->WaitUntilSleepingOrStopped();
  EXPECT_EQ(timer->CurrentState().deadline, zx::time(20));
  EXPECT_FALSE(timer->CurrentState().event_set);
  EXPECT_TRUE(timer->CurrentState().shutdown_set);

  timer->AdvanceTo(zx::time(20));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticTimerTest, Advance) {
  auto timer = SyntheticTimer::Create(zx::time(0));

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time(25));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(20));

    reason = timer->SleepUntil(zx::time(25));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(30));

    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  // Nothing yet.
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(10));

  // Event fires.
  timer->SetEventBit();
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(20));

  // Timer fires.
  timer->WaitUntilSleepingOrStopped();
  timer->AdvanceTo(zx::time(30));

  // Wait for the thread to complete.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

}  // namespace media_audio
