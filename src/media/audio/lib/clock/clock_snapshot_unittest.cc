// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/clock/clock_snapshot.h"

#include <lib/syslog/cpp/macros.h>

#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock.h"

using media::TimelineFunction;

namespace media_audio {
namespace {

TEST(ClockSnapshotTest, Create) {
  auto realm = SyntheticClockRealm::Create();
  realm->AdvanceBy(zx::msec(10));

  auto backing = realm->CreateClock("clock", Clock::kExternalDomain, true);
  auto to_mono_snapshot = backing->to_clock_mono_snapshot();

  ClockSnapshot snapshot(backing, realm->now());
  EXPECT_EQ(snapshot.name(), "clock");
  EXPECT_EQ(snapshot.domain(), Clock::kExternalDomain);
  EXPECT_EQ(snapshot.now(), realm->now());
  EXPECT_EQ(snapshot.to_clock_mono_snapshot().to_clock_mono, to_mono_snapshot.to_clock_mono);
  EXPECT_EQ(snapshot.to_clock_mono_snapshot().generation, to_mono_snapshot.generation);

  realm->AdvanceBy(zx::msec(10));
  backing->SetRate(1000);

  EXPECT_EQ(snapshot.name(), "clock");
  EXPECT_EQ(snapshot.domain(), Clock::kExternalDomain);
  EXPECT_EQ(snapshot.now(), realm->now() - zx::msec(10));
  EXPECT_EQ(snapshot.to_clock_mono_snapshot().to_clock_mono, to_mono_snapshot.to_clock_mono);
  EXPECT_EQ(snapshot.to_clock_mono_snapshot().generation, to_mono_snapshot.generation);
}

TEST(ClockSnapshotsTest, Update) {
  auto realm = SyntheticClockRealm::Create();
  realm->AdvanceBy(zx::msec(10));

  auto clock1 = realm->CreateClock("clock1", Clock::kExternalDomain, true);
  auto clock2 = realm->CreateClock("clock2", Clock::kExternalDomain, true);
  clock2->SetRate(1000);

  ClockSnapshots snapshots;
  snapshots.AddClock(clock1);
  snapshots.AddClock(clock2);

  realm->AdvanceBy(zx::msec(10));
  snapshots.Update(realm->now());

  auto snapshot1 = snapshots.SnapshotFor(clock1->koid());
  auto snapshot2 = snapshots.SnapshotFor(clock2->koid());

  EXPECT_EQ(snapshot1.name(), "clock1");
  EXPECT_EQ(snapshot1.koid(), clock1->koid());
  EXPECT_EQ(snapshot1.domain(), clock1->domain());
  EXPECT_EQ(snapshot1.now(), clock1->now());
  EXPECT_EQ(snapshot1.mono_now(), realm->now());
  EXPECT_EQ(snapshot1.to_clock_mono(), clock1->to_clock_mono());
  EXPECT_EQ(snapshot1.to_clock_mono_snapshot().generation, 0);

  EXPECT_EQ(snapshot2.name(), "clock2");
  EXPECT_EQ(snapshot2.koid(), clock2->koid());
  EXPECT_EQ(snapshot2.domain(), clock2->domain());
  EXPECT_EQ(snapshot2.now(), clock2->now());
  EXPECT_EQ(snapshot2.mono_now(), realm->now());
  EXPECT_EQ(snapshot2.to_clock_mono(), clock2->to_clock_mono());
  EXPECT_EQ(snapshot2.to_clock_mono_snapshot().generation, 1);

  realm->AdvanceBy(zx::msec(10));
  clock1->SetRate(-1000);
  clock2->SetRate(-1000);

  EXPECT_NE(snapshot1.now(), clock1->now());
  EXPECT_NE(snapshot1.mono_now(), realm->now());
  EXPECT_NE(snapshot1.to_clock_mono(), clock1->to_clock_mono());

  EXPECT_NE(snapshot2.now(), clock1->now());
  EXPECT_NE(snapshot2.mono_now(), realm->now());
  EXPECT_NE(snapshot2.to_clock_mono(), clock1->to_clock_mono());
}

}  // namespace
}  // namespace media_audio
