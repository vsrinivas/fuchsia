// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/gain_curve.h"

#include <fuchsia/media/cpp/fidl.h>

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/gain.h"

namespace media::audio {
namespace {

TEST(GainCurveTest, ValidationRejectsInsufficientMappings) {
  auto result1 = GainCurve::FromMappings({});
  ASSERT_TRUE(result1.is_error());
  EXPECT_EQ(result1.error(), GainCurve::kLessThanTwoMappingsCannotMakeCurve);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  EXPECT_EQ(result2.error(), GainCurve::kLessThanTwoMappingsCannotMakeCurve);
}

TEST(GainCurveTest, ValidationRejectsInsufficientDomain) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -10.0),
      GainCurve::VolumeMapping(0.5, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  EXPECT_EQ(result1.error(), GainCurve::kDomain0to1NotCovered);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.2, -0.45),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  EXPECT_EQ(result2.error(), GainCurve::kDomain0to1NotCovered);
}

TEST(GainCurveTest, ValidationRejectsInsufficientRange) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -10.0),
      GainCurve::VolumeMapping(1.0, -1.0),
  });
  ASSERT_TRUE(result1.is_error());
  EXPECT_EQ(result1.error(), GainCurve::kRange0NotCovered);
}

TEST(GainCurveTest, ValidationRejectsNonIncreasingDomains) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(0.2, -30.0),
      GainCurve::VolumeMapping(0.2, -31.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  EXPECT_EQ(result1.error(), GainCurve::kNonIncreasingDomainIllegal);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(0.2, -30.0),
      GainCurve::VolumeMapping(0.1, -31.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  EXPECT_EQ(result2.error(), GainCurve::kNonIncreasingDomainIllegal);
}

TEST(GainCurveTest, ValidationRejectsNonIncreasingRanges) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -2.0),
      GainCurve::VolumeMapping(0.2, -0.0),
      GainCurve::VolumeMapping(0.3, -0.1),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  EXPECT_EQ(result1.error(), GainCurve::kNonIncreasingRangeIllegal);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -2.0),
      GainCurve::VolumeMapping(0.1, -0.3),
      GainCurve::VolumeMapping(0.2, -0.3),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  EXPECT_EQ(result2.error(), GainCurve::kNonIncreasingRangeIllegal);
}

TEST(GainCurveTest, VolumeToDbBasic) {
  auto curve_result = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });

  ASSERT_TRUE(curve_result.is_ok());
  auto curve = curve_result.take_value();

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.0), -100.0);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.25), -75.0);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.5), -50.0);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.75), -25.0);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(1.0), 0.0);
}

TEST(GainCurveTest, DefaultCurves) {
  auto curve = GainCurve::Default();

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.0), fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(1.0), Gain::kUnityGainDb);

  const auto middle = curve.VolumeToDb(0.5);
  EXPECT_GT(middle, fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_LT(middle, 0.0);
}

TEST(GainCurveTest, DefaultCurveWithMinGainDb) {
  auto curve100 = GainCurve::DefaultForMinGain(-100.0);
  auto curve50 = GainCurve::DefaultForMinGain(-50.0);

  EXPECT_FLOAT_EQ(curve100.VolumeToDb(0.0), fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve100.VolumeToDb(0.0), fuchsia::media::audio::MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve50.VolumeToDb(1.0), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve50.VolumeToDb(1.0), Gain::kUnityGainDb);

  const auto middle100 = curve100.VolumeToDb(0.5);
  const auto middle50 = curve50.VolumeToDb(0.5);

  EXPECT_LT(middle100, middle50);
}

}  // namespace
}  // namespace media::audio
