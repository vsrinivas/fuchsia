// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/gain_control.h"

#include <lib/zx/time.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"
#include "src/media/audio/services/mixer/mix/testing/defaults.h"

namespace media_audio {
namespace {

using ::testing::AllOf;
using ::testing::Field;
using ::testing::FloatEq;
using ::testing::Matcher;
using ::testing::Optional;

Matcher<const GainControl::State&> StateEq(const GainControl::State& state) {
  return AllOf(Field(&GainControl::State::gain_db, FloatEq(state.gain_db)),
               Field(&GainControl::State::is_muted, state.is_muted),
               Field(&GainControl::State::linear_scale_slope_per_ns,
                     FloatEq(state.linear_scale_slope_per_ns)));
}

TEST(GainControlTest, ScheduleGain) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(1));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain.
  const float gain_db = 2.0f;
  gain_control.ScheduleGain(zx::time(5), gain_db);
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(5)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance before the scheduled time, gain should not be applied yet.
  gain_control.Advance(zx::time(2));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(5)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance to the scheduled time, gain should be applied now.
  gain_control.Advance(zx::time(5));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({gain_db, false, 0.0f}));

  // Advance further, gain should remain as-is.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({gain_db, false, 0.0f}));
}

TEST(GainControlTest, ScheduleGainWithRamp) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(1));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain with ramp.
  const float gain_db = ScaleToDb(11.0f);
  const zx::duration ramp_duration = zx::nsec(10);  // will result in a linear slope of 1.0 per ns.
  gain_control.ScheduleGain(zx::time(15), gain_db, GainRamp{ramp_duration});
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(15)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance before the scheduled time, gain should not be applied yet.
  gain_control.Advance(zx::time(2));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(15)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance to the scheduled time, ramp should start now.
  gain_control.Advance(zx::time(15));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 1.0f}));

  // Advance beyond the scheduled time, gain should be updated with the ramp.
  gain_control.Advance(zx::time(16));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(2.0f), false, 1.0f}));

  // Advance further but before the end of the ramp, gain should be updated with the same ramp.
  gain_control.Advance(zx::time(17));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(3.0f), false, 1.0f}));

  // Advance at the end of the ramp, gain should be updated with the completed ramp.
  gain_control.Advance(zx::time(25));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(11.0f), false, 0.0f}));

  // Finally advance beyond the end of the ramp, gain should remain as-is.
  gain_control.Advance(zx::time(30));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(11.0f), false, 0.0f}));
}

TEST(GainControlTest, ScheduleGainWithRampWithSingleAdvanceCall) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain with ramp.
  const float gain_db = ScaleToDb(11.0f);
  const zx::duration ramp_duration = zx::nsec(10);  // will result in a linear slope of 1.0 per ns.
  gain_control.ScheduleGain(zx::time(15), gain_db, GainRamp{ramp_duration});
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(15)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance beyond the end of the ramp, which should apply the completed gain ramp.
  gain_control.Advance(zx::time(30));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(11.0f), false, 0.0f}));
}

TEST(GainControlTest, ScheduleMute) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(1));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule mute.
  gain_control.ScheduleMute(zx::time(3), true);
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(3)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance before the scheduled time, mute should not be applied yet.
  gain_control.Advance(zx::time(2));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(3)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance to the scheduled time, mute should be applied now.
  gain_control.Advance(zx::time(3));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, true, 0.0f}));

  // Advance further, gain should remain as-is.
  gain_control.Advance(zx::time(5));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, true, 0.0f}));
}

TEST(GainControlTest, ScheduleBeforeAdvanceTime) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(5));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain at last advanced time.
  gain_control.ScheduleGain(zx::time(5), -1.0f);
  gain_control.Advance(zx::time(6));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({-1.0f, false, 0.0f}));

  // Schedule gain again at the same time, which should be applied at the next advanced time.
  gain_control.ScheduleGain(zx::time(5), 2.0f);
  gain_control.Advance(zx::time(7));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({2.0f, false, 0.0f}));

  // Schedule mute this time, again with the previous time, which should once again be applied at
  // the next advanced time.
  gain_control.ScheduleMute(zx::time(5), true);
  gain_control.Advance(zx::time(8));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({2.0f, true, 0.0f}));
}

TEST(GainControlTest, ScheduleBeforeAdvanceTimeOutOfOrder) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain changes in the past 2 nanoseconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    const auto time = zx::time((4 - i) * 2);
    gain_control.ScheduleGain(time, static_cast<float>(4 - i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(time));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Since all gain changes were scheduled in the past already, advance to apply them all in order.
  gain_control.Advance(zx::time(15));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, false, 0.0f}));

  // Now schedule mute changes in the past.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleMute(zx::time(2 * i + 1), i % 2);
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(1)));
    EXPECT_THAT(gain_control.state(), StateEq({4.0f, false, 0.0f}));
  }

  // Since all mute changes were also scheduled in the past, advance to apply them all in order.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, true, 0.0f}));
}

TEST(GainControlTest, ScheduleBeforeAdvanceTimeOutOfOrderWithSingleAdvanceCall) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain changes in the past 2 nanoseconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    const auto time = zx::time((4 - i) * 2);
    gain_control.ScheduleGain(time, static_cast<float>(4 - i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(time));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Schedule mute changes in the past in between.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleMute(zx::time(2 * i + 1), i % 2);
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(1)));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Since everything was scheduled in the past already, advance to apply them all in order.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, true, 0.0f}));
}

TEST(GainControlTest, ScheduleGainBeforeAdvanceTimeOutOfOrder) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain changes in the past 2 nanoseconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    const auto time = zx::time((4 - i) * 2);
    gain_control.ScheduleGain(time, static_cast<float>(4 - i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(time));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Since everything was scheduled in the past already, advance to apply them all in order.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, false, 0.0f}));
}

TEST(GainControlTest, ScheduleMuteBeforeAdvanceTimeOutOfOrder) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule mute changes in the past 2 nanoseconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    const auto time = zx::time((4 - i) * 2);
    gain_control.ScheduleMute(time, (4 - i) % 2 == 0);
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(time));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Since everything was scheduled in the past already, advance to apply them all in order.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, true, 0.0f}));
}

TEST(GainControlTest, ScheduleOutOfOrder) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule gain changes in the past 2 nanoseconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    const auto time = zx::time((4 - i) * 2);
    gain_control.ScheduleGain(time, static_cast<float>(4 - i));
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(time));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Schedule mute changes in the past in between.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleMute(zx::time(2 * i + 1), i % 2);
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(1)));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Schedule two more gain state at the same time of the first two changes, which should stay in
  // the same order as they were scheduled.
  gain_control.ScheduleGain(zx::time(1), -10.0f);
  gain_control.ScheduleGain(zx::time(2), -20.0f);
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(1)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance to a time in between to apply a subset of the scheduled changes.
  gain_control.Advance(zx::time(2));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(3)));
  EXPECT_THAT(gain_control.state(), StateEq({-20.0f, false, 0.0f}));

  // Advance further to apply another subset of the scheduled changes.
  gain_control.Advance(zx::time(4));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(5)));
  EXPECT_THAT(gain_control.state(), StateEq({2.0f, true, 0.0f}));

  // Finally advance beyond all scheduled changes to apply the rest of the changes in order.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, true, 0.0f}));
}

TEST(GainControlTest, ScheduleSameGain) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule the same gain multiple times from time 1 to 5.
  for (int i = 1; i <= 5; ++i) {
    gain_control.ScheduleGain(zx::time(i), 3.5f);
    EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(1)));
    EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));
  }

  // Advance beyond all scheduled gains, which should apply them all in order.
  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({3.5f, false, 0.0f}));
}

TEST(GainControlTest, ScheduleGainDuringRamp) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule constant gain.
  gain_control.ScheduleGain(zx::time(0), ScaleToDb(10.0f));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(0)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule another gain with ramp, which should result in a linear slope of -2.0 per ns, from the
  // constant gain value of 10.0.
  gain_control.ScheduleGain(zx::time(10), ScaleToDb(0.0f), GainRamp{zx::nsec(5)});
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(0)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule another gain with ramp during the previous ramp, which should result in a linear slope
  // of 1.0 per ns, starting from the midpoint gain value of 4.0 from the previous ramp.
  gain_control.ScheduleGain(zx::time(13), ScaleToDb(6.0f), GainRamp{zx::nsec(2)});
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(0)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Schedule one more constant gain *just* before the end of the previous ramp.
  gain_control.ScheduleGain(zx::time(15), ScaleToDb(8.0f));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(0)));
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance to the first scheduled ramp, which should start the ramp.
  gain_control.Advance(zx::time(10));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(13)));
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(10.0f), false, -2.0f}));

  // Advance to the second scheduled ramp, which should start from the midway of the first ramp.
  gain_control.Advance(zx::time(13));
  EXPECT_THAT(gain_control.NextScheduledStateChange(), Optional(zx::time(15)));
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(4.0f), false, 1.0f}));

  // Advance beyond all scheduled changes, which should apply the final constant value in order.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(8.0f), false, 0.0f}));
}

TEST(GainControlTest, SetGainAndMute) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Set gain.
  gain_control.SetGain(-6.0f);
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  gain_control.Advance(zx::time(1));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({-6.0f, false, 0.0f}));

  // Set mute.
  gain_control.SetMute(true);
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({-6.0f, false, 0.0f}));

  gain_control.Advance(zx::time(2));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({-6.0f, true, 0.0f}));

  // Set gain multiple times, where the last setting should override the rest.
  for (int i = 1; i <= 4; ++i) {
    gain_control.SetGain(static_cast<float>(i));
    EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
    EXPECT_THAT(gain_control.state(), StateEq({-6.0f, true, 0.0f}));
  }

  gain_control.Advance(zx::time(10));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, true, 0.0f}));

  // Toggle mute multiple times, where the last setting should override the rest.
  for (int i = 1; i <= 4; ++i) {
    gain_control.SetMute(i % 2);
    EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
    EXPECT_THAT(gain_control.state(), StateEq({4.0f, true, 0.0f}));
  }

  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({4.0f, false, 0.0f}));
}

TEST(GainControlTest, SetGainWithRamp) {
  GainControl gain_control(DefaultClock());
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Nothing scheduled yet.
  gain_control.Advance(zx::time(1));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Set gain with ramp.
  const float gain_db = ScaleToDb(6.0f);
  const zx::duration ramp_duration = zx::nsec(5);  // will result in a linear slope of 1.0 per ns.
  gain_control.SetGain(gain_db, GainRamp{ramp_duration});
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 0.0f}));

  // Advance any time to start the ramp.
  gain_control.Advance(zx::time(11));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({kUnityGainDb, false, 1.0f}));

  // Advance further but before the end of the ramp, gain should be updated with the same ramp.
  gain_control.Advance(zx::time(14));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(4.0f), false, 1.0f}));

  // Advance beyond the end of the ramp, which should complete the ramp.
  gain_control.Advance(zx::time(20));
  EXPECT_FALSE(gain_control.NextScheduledStateChange().has_value());
  EXPECT_THAT(gain_control.state(), StateEq({ScaleToDb(6.0f), false, 0.0f}));
}

}  // namespace
}  // namespace media_audio
