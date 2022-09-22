// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/fidl/gain_control_registry.h"

#include <lib/zx/time.h>

#include <string>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/common/basic_types.h"

namespace media_audio {
namespace {

TEST(GainControlRegistry, Advance) {
  GainControlRegistry gain_control_registry;

  auto clock_realm = SyntheticClockRealm::Create();
  ClockSnapshots clock_snapshots;
  for (uint64_t i = 1; i <= 4; ++i) {
    auto clock = clock_realm->CreateClock(std::to_string(i), Clock::kExternalDomain, false);
    gain_control_registry.Add(GainControlId{i}, UnreadableClock(clock));
    EXPECT_EQ(gain_control_registry.Get(GainControlId{i}).reference_clock(), clock);
    clock_snapshots.AddClock(std::move(clock));
  }
  clock_snapshots.Update(zx::time(0));

  // Schedule gains in descending monotonic time.
  for (uint64_t i = 1; i <= 4; ++i) {
    auto& gain_control = gain_control_registry.Get(GainControlId{i});
    const auto mono_time = zx::time(4u - i);
    const auto reference_time = clock_snapshots.SnapshotFor(gain_control.reference_clock())
                                    .ReferenceTimeFromMonotonicTime(mono_time);
    gain_control.ScheduleGain(reference_time, static_cast<float>(i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), testing::Optional(reference_time));
  }

  // Advance beyond all the scheduled changes.
  gain_control_registry.Advance(clock_snapshots, zx::time(5));
  for (uint64_t i = 1; i <= 4; ++i) {
    const auto& gain_control = gain_control_registry.Get(GainControlId{i});
    EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  }
}

}  // namespace
}  // namespace media_audio
