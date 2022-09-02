// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"

#include <lib/fit/defer.h>
#include <lib/sync/cpp/completion.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/zx/time.h>

#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/clone_mono.h"

using media::TimelineFunction;

namespace zx {
std::ostream& operator<<(std::ostream& os, time t) {
  os << t.get();
  return os;
}
}  // namespace zx

namespace media_audio {
namespace {

TEST(SyntheticClockRealmTest, AdvanceMultipleClocks) {
  auto realm = SyntheticClockRealm::Create();
  auto clock1 = realm->CreateClock("clock1", Clock::kExternalDomain, true);
  EXPECT_EQ(realm->now(), zx::time(0));
  EXPECT_EQ(clock1->now(), zx::time(0));

  realm->AdvanceBy(zx::nsec(10));
  auto clock2 = realm->CreateClock("clock2", Clock::kExternalDomain, true);
  EXPECT_EQ(realm->now(), zx::time(10));
  EXPECT_EQ(clock1->now(), zx::time(10));
  EXPECT_EQ(clock2->now(), zx::time(10));

  realm->AdvanceTo(zx::time(50));
  EXPECT_EQ(realm->now(), zx::time(50));
  EXPECT_EQ(clock1->now(), zx::time(50));
  EXPECT_EQ(clock2->now(), zx::time(50));
}

TEST(SyntheticClockRealmTest, AdvanceMultipleTimers) {
  auto realm = SyntheticClockRealm::Create();
  auto clock = realm->CreateClock("clock", Clock::kExternalDomain, true);
  auto timer1 = realm->CreateTimer();
  auto timer2 = realm->CreateTimer();

  EXPECT_EQ(realm->now(), zx::time(0));
  EXPECT_EQ(clock->now(), zx::time(0));
  EXPECT_EQ(timer1->now(), zx::time(0));
  EXPECT_EQ(timer2->now(), zx::time(0));

  libsync::Completion done1;
  libsync::Completion done2;

  std::thread thread1([&timer1, &done1, &clock]() mutable {
    auto reason = timer1->SleepUntil(zx::time::infinite());
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer1->now(), zx::time(0));
    EXPECT_EQ(clock->now(), zx::time(0));

    reason = timer1->SleepUntil(zx::time(15));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer1->now(), zx::time(15));
    EXPECT_EQ(clock->now(), zx::time(15));

    reason = timer1->SleepUntil(zx::time(50));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer1->now(), zx::time(30));
    EXPECT_EQ(clock->now(), zx::time(30));

    timer1->Stop();
    done1.Signal();
  });

  std::thread thread2([&timer2, &done2, &clock]() mutable {
    auto reason = timer2->SleepUntil(zx::time(5));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer2->now(), zx::time(5));
    EXPECT_EQ(clock->now(), zx::time(5));

    reason = timer2->SleepUntil(zx::time(25));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer2->now(), zx::time(25));
    EXPECT_EQ(clock->now(), zx::time(25));

    reason = timer2->SleepUntil(zx::time(50));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_TRUE(reason.shutdown_set);
    EXPECT_EQ(timer2->now(), zx::time(30));
    EXPECT_EQ(clock->now(), zx::time(30));

    timer2->Stop();
    done2.Signal();
  });

  auto join1 = fit::defer([&thread1]() { thread1.join(); });
  auto join2 = fit::defer([&thread2]() { thread2.join(); });

  // This should wake timer1 immediately, then timer2 at t=5.
  timer1->SetEventBit();
  realm->AdvanceTo(zx::time(10));
  EXPECT_EQ(realm->now(), zx::time(10));

  // This should wake timer1 at t=15, then timer2 at t=25.
  realm->AdvanceTo(zx::time(30));
  EXPECT_EQ(realm->now(), zx::time(30));

  // This should wake both timers immediately.
  timer1->SetShutdownBit();
  timer2->SetShutdownBit();
  realm->AdvanceTo(zx::time(50));
  EXPECT_EQ(realm->now(), zx::time(50));

  // Wait for the threads to complete.
  EXPECT_EQ(done1.Wait(zx::sec(5)), ZX_OK);
  EXPECT_EQ(done2.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticClockRealmTest, AdvanceTimerWithRecursiveEvents) {
  auto realm = SyntheticClockRealm::Create();
  auto timer = realm->CreateTimer();

  libsync::Completion done1;
  libsync::Completion done2;

  std::thread thread([&timer, &done1, &done2]() mutable {
    auto reason = timer->SleepUntil(zx::time(10));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));

    // Queuing another event should cause SleepUntil to return immediately.
    timer->SetEventBit();
    reason = timer->SleepUntil(zx::time(10));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));

    // Once more.
    timer->SetEventBit();
    reason = timer->SleepUntil(zx::time(10));
    EXPECT_FALSE(reason.deadline_expired);
    EXPECT_TRUE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(0));
    done1.Signal();

    // Now wait until the timer fires.
    reason = timer->SleepUntil(zx::time(10));
    EXPECT_TRUE(reason.deadline_expired);
    EXPECT_FALSE(reason.event_set);
    EXPECT_FALSE(reason.shutdown_set);
    EXPECT_EQ(timer->now(), zx::time(10));

    timer->Stop();
    done2.Signal();
  });

  auto join = fit::defer([&thread]() { thread.join(); });

  // This should wake the timer immediately. AdvanceTo should not return until events have quiesced.
  timer->SetEventBit();
  realm->AdvanceTo(zx::time(0));
  EXPECT_EQ(realm->now(), zx::time(0));
  EXPECT_EQ(done1.Wait(zx::sec(5)), ZX_OK);

  // This should wake the timer at t=10.
  realm->AdvanceTo(zx::time(10));
  EXPECT_EQ(realm->now(), zx::time(10));
  EXPECT_EQ(done2.Wait(zx::sec(5)), ZX_OK);
}

TEST(SyntheticClockRealmTest, AdvanceWithConcurrentCreateTimer) {
  auto realm = SyntheticClockRealm::Create();
  EXPECT_EQ(realm->now(), zx::time(0));

  std::shared_ptr<SyntheticTimer> timer;
  libsync::Completion done;
  std::thread thread([&timer, &done, realm]() mutable {
    timer = realm->CreateTimer();
    timer->Stop();
    done.Signal();
  });
  auto join = fit::defer([&thread]() { thread.join(); });

  // This happens concurrently with the CreateTimer call.
  realm->AdvanceTo(zx::time(10));
  EXPECT_EQ(realm->now(), zx::time(10));

  // No matter which order Create and Advance are completed, the timer must have advanced.
  EXPECT_EQ(done.Wait(zx::sec(5)), ZX_OK);
  EXPECT_EQ(timer->now(), zx::time(10));
}

}  // namespace
}  // namespace media_audio
