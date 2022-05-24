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

}  // namespace
}  // namespace media_audio
