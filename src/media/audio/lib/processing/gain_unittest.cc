// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/processing/gain.h"

#include <vector>

#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(GainTest, ApplyGainSilent) {
  const std::vector<float> values = {-0.5f, 0.2f, 1.0f, 2.0f};
  const std::vector<float> scales = {0.0f, 0.5f, 1.0f, 1.5f};

  for (const float value : values) {
    for (const float scale : scales) {
      EXPECT_FLOAT_EQ(ApplyGain<GainType::kSilent>(value, scale), 0.0f);
    }
  }
}

TEST(GainTest, ApplyGainNonUnity) {
  const std::vector<float> values = {-1.0f, -0.25f, 0.5f, 1.5f};
  const std::vector<float> scales = {0.0f, 0.5f, 1.0f, 1.5f};

  for (const float value : values) {
    for (const float scale : scales) {
      EXPECT_FLOAT_EQ(ApplyGain<GainType::kNonUnity>(value, scale), value * scale);
    }
  }
}

TEST(GainTest, ApplyGainUnity) {
  const std::vector<float> values = {-0.1f, 0.2f, -0.3f, 1.0f};
  const std::vector<float> scales = {0.0f, 0.5f, 1.0f, 0.5f};

  for (const float value : values) {
    for (const float scale : scales) {
      EXPECT_FLOAT_EQ(ApplyGain<GainType::kUnity>(value, scale), value);
    }
  }
}

TEST(GainTest, ApplyGainRamping) {
  const std::vector<float> values = {-0.5f, 0.2f, 1.0f, 2.0f};
  const std::vector<float> scales = {0.0f, 0.5f, 1.0f, 1.5f};

  for (const float value : values) {
    for (const float scale : scales) {
      EXPECT_FLOAT_EQ(ApplyGain<GainType::kRamping>(value, scale), value * scale);
    }
  }
}

TEST(GainTest, DbToScale) {
  const float gain_db = -6.0f;
  const float gain_scale = 0.5f;

  const float epsilon = 5e-2f;
  EXPECT_NEAR(DbToScale(gain_db), gain_scale, epsilon);
  EXPECT_NEAR(ScaleToDb(gain_scale), gain_db, epsilon);

  // Verify back and forth conversions.
  EXPECT_FLOAT_EQ(DbToScale(ScaleToDb(gain_scale)), gain_scale);
  EXPECT_FLOAT_EQ(ScaleToDb(DbToScale(gain_db)), gain_db);
}

TEST(GainTest, DbToScaleMinGain) {
  // Verify that the values are clamped at minimum gain.
  EXPECT_FLOAT_EQ(DbToScale(kMinGainDb), 0.0f);
  EXPECT_FLOAT_EQ(DbToScale(kMinGainDb - 12.0f), 0.0f);

  EXPECT_FLOAT_EQ(ScaleToDb(kMinGainScale), kMinGainDb);
  EXPECT_FLOAT_EQ(ScaleToDb(0.0f), kMinGainDb);
  EXPECT_FLOAT_EQ(ScaleToDb(-1.0f), kMinGainDb);

  // Verify back and forth conversions at minimum gain.
  EXPECT_FLOAT_EQ(DbToScale(ScaleToDb(0.0f)), 0.0f);
  EXPECT_FLOAT_EQ(DbToScale(ScaleToDb(kMinGainScale)), 0.0f);
  EXPECT_FLOAT_EQ(ScaleToDb(DbToScale(kMinGainDb)), kMinGainDb);
}

}  // namespace
}  // namespace media_audio
