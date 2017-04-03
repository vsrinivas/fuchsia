// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/audio/level.h"

#include <cmath>
#include <limits>

#include "apps/media/src/audio/gain.h"
#include "apps/media/src/audio/test/test_utils.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Tests expectations regarding distinguished values of Level.
TEST(LevelTest, DistinguishedValues) {
  // This value should be identical to the one in Level::FromGain (level.cc).
  static constexpr float kHighestSilentGain = -451.545f;

  // Default constructor creates silence.
  EXPECT_EQ(Level<float>::Silence, Level<float>());

  // Unity exceeds silence.
  EXPECT_GT(Level<float>::Unity, Level<float>::Silence);

  // Unity equals gain of 0.
  EXPECT_EQ(Level<float>::Unity, Level<float>::FromGain(Gain::Unity));
  EXPECT_EQ(Gain::Unity, Level<float>::Unity.ToGain());

  // FromGain and ToGain round-trip silence.
  EXPECT_EQ(Level<float>::Silence,
            Level<float>::FromGain(Level<float>::Silence.ToGain()));

  // FromGain and ToGain round-trip unity.
  EXPECT_EQ(Level<float>::Unity,
            Level<float>::FromGain(Level<float>::Unity.ToGain()));

  // kHighestSilentGain should produce a float level value of zero using the
  // canonical formula, and slightly higher values should not.
  EXPECT_EQ(0.0f, std::pow(10.0f, kHighestSilentGain / 10.0f));
  EXPECT_NE(0.0f, std::pow(10.0f, (kHighestSilentGain + 0.001f) / 10.0f));
}

// Tests expectations regarding arbitrary values of Level.
TEST(LevelTest, ArbitraryValues) {
  Level<float> previous_level = Level<float>(0.0f);

  for (float level_value = 0.001f; level_value <= 50.0f;
       level_value += 0.001f) {
    Level<float> level = Level<float>(level_value);

    // FromGain and ToGain round-trip this value, with some error.
    EXPECT_TRUE(RoughlyEquals(level.value(),
                              Level<float>::FromGain(level.ToGain()).value(),
                              0.00002f));

    // This level should be greater than the previous one.
    EXPECT_GT(level, previous_level);

    previous_level = level;
  }
}

}  // namespace test
}  // namespace media
