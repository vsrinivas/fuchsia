// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/gain_control.h"

#include <lib/zx/time.h>

#include <optional>
#include <utility>
#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"

namespace media_audio {
namespace {

using ::testing::AllOf;
using ::testing::ElementsAre;
using ::testing::Field;
using ::testing::FloatEq;
using ::testing::Matcher;
using ::testing::Pair;

Matcher<const GainControl::State&> StateEq(const GainControl::State& state) {
  return AllOf(Field(&GainControl::State::gain_db, FloatEq(state.gain_db)),
               Field(&GainControl::State::is_muted, state.is_muted),
               Field(&GainControl::State::linear_scale_slope_per_ns,
                     FloatEq(state.linear_scale_slope_per_ns)));
}

TEST(GainControlTest, ScheduleGain) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(1), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Schedule gain.
  const float gain_db = 2.0f;
  gain_control.ScheduleGain(zx::time(5), gain_db);
  EXPECT_THAT(states, ElementsAre());
  states.clear();

  // Process before the scheduled time, gain should not be applied yet.
  gain_control.Process(zx::time(1), zx::time(2), callback);
  EXPECT_THAT(states, ElementsAre(Pair(1, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process *just* before the scheduled time, gain should still not be applied.
  gain_control.Process(zx::time(2), zx::time(5), callback);
  EXPECT_THAT(states, ElementsAre(Pair(2, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process beyond the scheduled time, gain should be applied now.
  gain_control.Process(zx::time(5), zx::time(6), callback);
  EXPECT_THAT(states, ElementsAre(Pair(5, StateEq({gain_db, false, 0.0f}))));
  states.clear();

  // Process further, gain should remain as-is.
  gain_control.Process(zx::time(6), zx::time(10), callback);
  EXPECT_THAT(states, ElementsAre(Pair(6, StateEq({gain_db, false, 0.0f}))));
}

TEST(GainControlTest, ScheduleGainWithRamp) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(1), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Schedule gain with ramp.
  const float gain_db = ScaleToDb(11.0f);
  const zx::duration ramp_duration = zx::nsec(10);  // will result in a linear slope of 1.0 per ns.
  gain_control.ScheduleGain(zx::time(15), gain_db, GainRamp{ramp_duration});
  EXPECT_THAT(states, ElementsAre());
  states.clear();

  // Process before the scheduled time, gain should not be applied yet.
  gain_control.Process(zx::time(1), zx::time(2), callback);
  EXPECT_THAT(states, ElementsAre(Pair(1, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process *just* before the scheduled time, gain should still not be applied.
  gain_control.Process(zx::time(2), zx::time(15), callback);
  EXPECT_THAT(states, ElementsAre(Pair(2, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process beyond the scheduled time, ramp should start now.
  gain_control.Process(zx::time(15), zx::time(16), callback);
  EXPECT_THAT(states, ElementsAre(Pair(15, StateEq({kUnityGainDb, false, 1.0f}))));
  states.clear();

  // Process further but before the end of the ramp, gain should be updated with the same ramp.
  gain_control.Process(zx::time(16), zx::time(17), callback);
  EXPECT_THAT(states, ElementsAre(Pair(16, StateEq({ScaleToDb(2.0f), false, 1.0f}))));
  states.clear();

  // Process *just* before the end of the ramp, gain still should be updated with the same ramp.
  gain_control.Process(zx::time(17), zx::time(25), callback);
  EXPECT_THAT(states, ElementsAre(Pair(17, StateEq({ScaleToDb(3.0f), false, 1.0f}))));
  states.clear();

  // Finally process beyond the end of the ramp, which should trigger ramp completion.
  gain_control.Process(zx::time(25), zx::time(30), callback);
  EXPECT_THAT(states, ElementsAre(Pair(25, StateEq({gain_db, false, 0.0f}))));
}

TEST(GainControlTest, ScheduleGainWithRampWithSingleProcessCall) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Schedule gain with ramp.
  const float gain_db = ScaleToDb(11.0f);
  const zx::duration ramp_duration = zx::nsec(10);  // will result in a linear slope of 1.0 per ns.
  gain_control.ScheduleGain(zx::time(15), gain_db, GainRamp{ramp_duration});

  // Process beyond the end of the ramp, which should trigger all state changes of the gain ramp.
  gain_control.Process(zx::time(0), zx::time(30), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f})),
                                  Pair(15, StateEq({kUnityGainDb, false, 1.0f})),
                                  Pair(25, StateEq({gain_db, false, 0.0f}))));
}

TEST(GainControlTest, ScheduleMute) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(1), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Schedule mute.
  gain_control.ScheduleMute(zx::time(3), true);
  EXPECT_THAT(states, ElementsAre());
  states.clear();

  // Process before the scheduled time, gain should not be applied yet.
  gain_control.Process(zx::time(1), zx::time(2), callback);
  EXPECT_THAT(states, ElementsAre(Pair(1, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process *just* before the scheduled time, gain should still not be applied.
  gain_control.Process(zx::time(2), zx::time(3), callback);
  EXPECT_THAT(states, ElementsAre(Pair(2, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Process beyond the scheduled time, gain should be applied now.
  gain_control.Process(zx::time(3), zx::time(5), callback);
  EXPECT_THAT(states, ElementsAre(Pair(3, StateEq({kUnityGainDb, true, 0.0f}))));
  states.clear();

  // Process further, gain should remain as-is.
  gain_control.Process(zx::time(5), zx::time(10), callback);
  EXPECT_THAT(states, ElementsAre(Pair(5, StateEq({kUnityGainDb, true, 0.0f}))));
}

TEST(GainControlTest, ScheduleBeforeProcessTime) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(5), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Schedule gain at last advanced time.
  gain_control.ScheduleGain(zx::time(5), 1.0f);
  gain_control.Process(zx::time(5), zx::time(6), callback);
  EXPECT_THAT(states, ElementsAre(Pair(5, StateEq({1.0f, false, 0.0f}))));
  states.clear();

  // Schedule gain again at the same time, which should be reported at the next start time.
  gain_control.ScheduleGain(zx::time(5), 2.0f);
  gain_control.Process(zx::time(6), zx::time(7), callback);
  EXPECT_THAT(states, ElementsAre(Pair(6, StateEq({2.0f, false, 0.0f}))));
  states.clear();

  // Schedule mute this time, again with the previous time, which should once again be reported at
  // the next start time.
  gain_control.ScheduleMute(zx::time(5), true);
  gain_control.Process(zx::time(7), zx::time(8), callback);
  EXPECT_THAT(states, ElementsAre(Pair(7, StateEq({2.0f, true, 0.0f}))));
  states.clear();
}

TEST(GainControlTest, ScheduleBeforeProcessTimeOutOfOrder) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(10), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Schedule gain events in the past 2 seconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleGain(zx::time((4 - i) * 2), static_cast<float>(4 - i));
  }

  // Schedule mute events in the past in between.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleMute(zx::time(2 * i + 1), i % 2);
  }

  // Since everything was scheduled in the past, only the latest merged state will be reported.
  gain_control.Process(zx::time(10), zx::time(20), callback);
  EXPECT_THAT(states, ElementsAre(Pair(10, StateEq({4.0f, true, 0.0f}))));
}

TEST(GainControlTest, ScheduleOutOfOrder) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Schedule gain events in the past 2 seconds apart in reverse order.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleGain(zx::time((4 - i) * 2), static_cast<float>(4 - i));
  }

  // Schedule mute events in the past in between.
  for (int i = 0; i < 4; ++i) {
    gain_control.ScheduleMute(zx::time(2 * i + 1), i % 2);
  }

  // Schedule two more gain state at the same time of the first two events, which should stay in the
  // same order as they were scheduled.
  gain_control.ScheduleGain(zx::time(1), -10.0f);
  gain_control.ScheduleGain(zx::time(2), -20.0f);

  // Process beyond all scheduled events, which should process them all in order.
  gain_control.Process(zx::time(0), zx::time(10), callback);
  EXPECT_THAT(
      states,
      ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f})),
                  Pair(1, StateEq({-10.0f, false, 0.0f})), Pair(2, StateEq({-20.0f, false, 0.0f})),
                  Pair(3, StateEq({-20.0f, true, 0.0f})), Pair(4, StateEq({2.0f, true, 0.0f})),
                  Pair(5, StateEq({2.0f, false, 0.0f})), Pair(6, StateEq({3.0f, false, 0.0f})),
                  Pair(7, StateEq({3.0f, true, 0.0f})), Pair(8, StateEq({4.0f, true, 0.0f}))));
}

TEST(GainControlTest, ScheduleSameGain) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Schedule the same gain multiple times from time 1 to 5.
  for (int i = 1; i <= 5; ++i) {
    gain_control.ScheduleGain(zx::time(i), 3.5f);
  }

  // Process beyond all scheduled gains, which shouldd only report a single gain change.
  gain_control.Process(zx::time(0), zx::time(10), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f})),
                                  Pair(1, StateEq({3.5f, false, 0.0f}))));
}

TEST(GainControlTest, ScheduleGainDuringRamp) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Schedule constant gain.
  gain_control.ScheduleGain(zx::time(0), ScaleToDb(10.0f));

  // Schedule another gain with ramp, which should result in a linear slope of -2.0 per ns, from the
  // constant gain value of 10.0.
  gain_control.ScheduleGain(zx::time(10), ScaleToDb(0.0f), GainRamp{zx::nsec(5)});

  // Schedule another gain with ramp during the previous ramp, which should result in a linear slope
  // of 1.0 per ns, starting from the midpoint gain value of 4.0 from the previous ramp.
  gain_control.ScheduleGain(zx::time(13), ScaleToDb(6.0f), GainRamp{zx::nsec(2)});

  // Schedule one more constant gain *just* before the end of the previous ramp.
  gain_control.ScheduleGain(zx::time(15), ScaleToDb(8.0f));

  // Process beyond all scheduled events, which should process them all.
  gain_control.Process(zx::time(0), zx::time(20), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({ScaleToDb(10.0f), false, 0.0f})),
                                  Pair(10, StateEq({ScaleToDb(10.0f), false, -2.0f})),
                                  Pair(13, StateEq({ScaleToDb(4.0f), false, 1.0f})),
                                  Pair(15, StateEq({ScaleToDb(8.0f), false, 0.0f}))));
}

TEST(GainControlTest, SetGainAndMute) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Set gain.
  gain_control.SetGain(-6.0f);
  gain_control.Process(zx::time(0), zx::time(1), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({-6.0f, false, 0.0f}))));
  states.clear();

  // Set mute.
  gain_control.SetMute(true);
  gain_control.Process(zx::time(1), zx::time(2), callback);
  EXPECT_THAT(states, ElementsAre(Pair(1, StateEq({-6.0f, true, 0.0f}))));
  states.clear();

  // Set gain multiple times, where the last setting should override the rest.
  for (int i = 1; i <= 4; ++i) {
    gain_control.SetGain(static_cast<float>(i));
  }
  gain_control.Process(zx::time(5), zx::time(10), callback);
  EXPECT_THAT(states, ElementsAre(Pair(5, StateEq({4.0f, true, 0.0f}))));
  states.clear();

  // Toggle mute multiple times, where the last setting should override the rest.
  for (int i = 1; i <= 4; ++i) {
    gain_control.SetMute(i % 2);
  }
  gain_control.Process(zx::time(10), zx::time(20), callback);
  EXPECT_THAT(states, ElementsAre(Pair(10, StateEq({4.0f, false, 0.0f}))));
}

TEST(GainControlTest, SetGainWithRamp) {
  GainControl gain_control;
  std::vector<std::pair<int64_t, GainControl::State>> states;
  const auto callback = [&](zx::time reference_time, const GainControl::State& state) {
    states.emplace_back((reference_time - zx::time(0)).to_nsecs(), state);
  };

  // Nothing scheduled yet.
  gain_control.Process(zx::time(0), zx::time(1), callback);
  EXPECT_THAT(states, ElementsAre(Pair(0, StateEq({kUnityGainDb, false, 0.0f}))));
  states.clear();

  // Set gain with ramp.
  const float gain_db = ScaleToDb(6.0f);
  const zx::duration ramp_duration = zx::nsec(5);  // will result in a linear slope of 1.0 per ns.
  gain_control.SetGain(gain_db, GainRamp{ramp_duration});
  EXPECT_THAT(states, ElementsAre());
  states.clear();

  // Process for one second skipping 10 seconds, ramp should start immediately.
  gain_control.Process(zx::time(11), zx::time(12), callback);
  EXPECT_THAT(states, ElementsAre(Pair(11, StateEq({kUnityGainDb, false, 1.0f}))));
  states.clear();

  // Process further but before the end of the ramp, gain should be updated with the same ramp.
  gain_control.Process(zx::time(12), zx::time(14), callback);
  EXPECT_THAT(states, ElementsAre(Pair(12, StateEq({ScaleToDb(2.0f), false, 1.0f}))));
  states.clear();

  // Process beyond the end of the ramp, which should trigger ramp completion.
  gain_control.Process(zx::time(14), zx::time(20), callback);
  EXPECT_THAT(states, ElementsAre(Pair(14, StateEq({ScaleToDb(4.0f), false, 1.0f})),
                                  Pair(16, StateEq({ScaleToDb(6.0f), false, 0.0f}))));
}

}  // namespace
}  // namespace media_audio
