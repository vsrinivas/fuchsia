// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/mixer_gain_controls.h"

#include <lib/zx/time.h>

#include <string>
#include <unordered_set>
#include <utility>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/clock/synthetic_clock_realm.h"
#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/common/basic_types.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::testing::Optional;

TEST(MixerGainControlsTest, AddWithInitialState) {
  MixerGainControls mixer_gain_controls;

  // Advance without any gain controls.
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(1));
  EXPECT_FALSE(mixer_gain_controls.NextScheduledStateChange(DefaultClockSnapshots()).has_value());

  // Create a gain control with an initial state.
  GainControl gain_control(DefaultClock());
  gain_control.SetGain(-1.0f);
  gain_control.ScheduleGain(zx::time(5), 5.0f);
  gain_control.Advance(zx::time(1));

  // Add the gain control and schedule another gain change.
  const auto gain_id = GainControlId{1};
  mixer_gain_controls.Add(gain_id, std::move(gain_control));
  mixer_gain_controls.Get(gain_id).ScheduleGain(zx::time(2), 2.0f);
  EXPECT_FLOAT_EQ(mixer_gain_controls.Get(gain_id).state().gain_db, -1.0f);
  EXPECT_THAT(mixer_gain_controls.NextScheduledStateChange(DefaultClockSnapshots()),
              Optional(zx::time(2)));

  // Advance to the next gain change.
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(2));
  EXPECT_FLOAT_EQ(mixer_gain_controls.Get(gain_id).state().gain_db, 2.0f);
  EXPECT_THAT(mixer_gain_controls.NextScheduledStateChange(DefaultClockSnapshots()),
              Optional(zx::time(5)));

  // Advance to the previously scheduled gain change.
  mixer_gain_controls.Advance(DefaultClockSnapshots(), zx::time(5));
  EXPECT_FLOAT_EQ(mixer_gain_controls.Get(gain_id).state().gain_db, 5.0f);
  EXPECT_FALSE(mixer_gain_controls.NextScheduledStateChange(DefaultClockSnapshots()).has_value());
}

TEST(MixerGainControlsTest, Advance) {
  MixerGainControls mixer_gain_controls;

  auto clock_realm = SyntheticClockRealm::Create();
  ClockSnapshots clock_snapshots;
  for (uint64_t i = 1; i <= 4; ++i) {
    auto clock = clock_realm->CreateClock(std::to_string(i), Clock::kExternalDomain, false);
    mixer_gain_controls.Add(GainControlId{i}, GainControl(UnreadableClock(clock)));
    clock_snapshots.AddClock(std::move(clock));
  }
  clock_snapshots.Update(zx::time(0));
  EXPECT_FALSE(mixer_gain_controls.NextScheduledStateChange(clock_snapshots).has_value());

  // Schedule gains in descending monotonic time.
  for (uint64_t i = 1; i <= 4; ++i) {
    auto& gain_control = mixer_gain_controls.Get(GainControlId{i});

    const auto mono_time = zx::time(4u - i);
    const auto reference_time = clock_snapshots.SnapshotFor(gain_control.reference_clock())
                                    .ReferenceTimeFromMonotonicTime(mono_time);
    gain_control.ScheduleGain(reference_time, static_cast<float>(i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(reference_time));
    EXPECT_THAT(mixer_gain_controls.NextScheduledStateChange(clock_snapshots), Optional(mono_time));
  }

  // Advance to the next scheduled time.
  auto next_scheduled_time = mixer_gain_controls.NextScheduledStateChange(clock_snapshots);
  ASSERT_THAT(next_scheduled_time, Optional(zx::time(0)));
  mixer_gain_controls.Advance(clock_snapshots, *next_scheduled_time);
  EXPECT_THAT(mixer_gain_controls.NextScheduledStateChange(clock_snapshots), Optional(zx::time(1)));

  // Advance beyond all the scheduled changes.
  mixer_gain_controls.Advance(clock_snapshots, zx::time(5));
  EXPECT_FALSE(mixer_gain_controls.NextScheduledStateChange(clock_snapshots).has_value());
}

}  // namespace
}  // namespace media_audio
