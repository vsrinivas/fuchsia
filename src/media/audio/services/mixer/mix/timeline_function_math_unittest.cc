// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/services/mixer/mix/timeline_function_math.h"

#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(TimelineFunctionOffsetInFracFramesTest, SubjectTimeAhead) {
  auto slope = TimelineRate(Fixed(1).raw_value(), 5);
  auto a = TimelineFunction(Fixed(20).raw_value(), 10, slope);
  auto b = TimelineFunction(Fixed(38).raw_value(), 60, slope);

  // When x=10 advances to x=60, y=20 should advance to y=20+50/5=30. Instead it advances to y=38.
  // Hence, the offset is 8.
  EXPECT_EQ(TimelineFunctionOffsetInFracFrames(a, b), Fixed(8));
}

TEST(TimelineFunctionOffsetInFracFramesTest, SubjectTimeBehind) {
  auto slope = TimelineRate(Fixed(1).raw_value(), 5);
  auto a = TimelineFunction(Fixed(20).raw_value(), 10, slope);
  auto b = TimelineFunction(Fixed(26).raw_value(), 60, slope);

  // When x=10 advances to x=60, y=20 should advance to y=20+50/5=30. Instead it advances to y=26.
  // Hence, the offset is -4.
  EXPECT_EQ(TimelineFunctionOffsetInFracFrames(a, b), Fixed(-4));
}

}  // namespace
}  // namespace media_audio
