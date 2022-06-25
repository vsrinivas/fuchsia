// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/real_timer.h"

#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <atomic>
#include <thread>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media_audio {

TEST(RealTimerTest, Event) {
  auto timer = RealTimer::Create({});

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetEventBit();
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, Shutdown) {
  auto timer = RealTimer::Create({});

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  timer->SetShutdownBit();
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, Timer) {
  auto timer = RealTimer::Create({});

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, EventThenTimer) {
  auto timer = RealTimer::Create({});
  timer->SetEventBit();

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    // SetEventBit happened before SleepUntil, therefore this should return immediately.
    auto reason = timer->SleepUntil(zx::deadline_after(zx::sec(1)));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);

    // The event bit should be cleared by the prior SleepUntil, so only the timer should fire.
    reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, ShutdownThenTimer) {
  auto timer = RealTimer::Create({});
  timer->SetShutdownBit();

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    // SetShutdownBit happened before SleepUntil, therefore this should return immediately.
    auto reason = timer->SleepUntil(zx::deadline_after(zx::sec(1)));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);

    // The shutdown bit should persist, therefore we should return immediately again.
    reason = timer->SleepUntil(zx::deadline_after(zx::sec(1)));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, TimerThenEvent) {
  auto timer = RealTimer::Create({});

  libsync::Completion done1;
  libsync::Completion done2;

  std::thread thread([&timer, &done1, &done2]() mutable {
    auto reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done1.Signal();

    reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done2.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done1.Wait(zx::sec(5)), ZX_OK);
  timer->SetEventBit();
  EXPECT_EQ(done2.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, TimerThenShutdown) {
  auto timer = RealTimer::Create({});

  libsync::Completion done1;
  libsync::Completion done2;

  std::thread thread([&timer, &done1, &done2]() mutable {
    auto reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done1.Signal();

    reason = timer->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    done2.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done1.Wait(zx::sec(5)), ZX_OK);
  timer->SetShutdownBit();
  EXPECT_EQ(done2.Wait(zx::sec(5)), ZX_OK);
}

TEST(RealTimerTest, TimerThenTimer) {
  auto timer = RealTimer::Create({});

  libsync::Completion done;

  std::thread thread([&timer, &done]() mutable {
    auto reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);

    reason = timer->SleepUntil(zx::deadline_after(zx::msec(10)));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
}

}  // namespace media_audio
