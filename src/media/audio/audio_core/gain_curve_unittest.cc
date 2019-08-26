// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/gain_curve.h"

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(GainCurveTest, ValidationRejectsInsufficientMappings) {
  auto result1 = GainCurve::FromMappings({});
  ASSERT_TRUE(result1.is_error());
  ASSERT_EQ(result1.error(), GainCurve::kLessThanTwoMappingsCannotMakeCurve);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  ASSERT_EQ(result2.error(), GainCurve::kLessThanTwoMappingsCannotMakeCurve);
}

TEST(GainCurveTest, ValidationRejectsInsufficientDomain) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -10.0),
      GainCurve::VolumeMapping(0.5, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  ASSERT_EQ(result1.error(), GainCurve::kDomain0to1NotCovered);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.2, -0.45),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  ASSERT_EQ(result2.error(), GainCurve::kDomain0to1NotCovered);
}

TEST(GainCurveTest, ValidationRejectsInsufficientRange) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -10.0),
      GainCurve::VolumeMapping(1.0, -1.0),
  });
  ASSERT_TRUE(result1.is_error());
  ASSERT_EQ(result1.error(), GainCurve::kRange0NotCovered);
}

TEST(GainCurveTest, ValidationRejectsNonIncreasingDomains) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(0.2, -30.0),
      GainCurve::VolumeMapping(0.2, -31.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  ASSERT_EQ(result1.error(), GainCurve::kNonIncreasingDomainIllegal);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(0.2, -30.0),
      GainCurve::VolumeMapping(0.1, -31.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  ASSERT_EQ(result2.error(), GainCurve::kNonIncreasingDomainIllegal);
}

TEST(GainCurveTest, ValidationRejectsNonIncreasingRanges) {
  auto result1 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -2.0),
      GainCurve::VolumeMapping(0.2, -0.0),
      GainCurve::VolumeMapping(0.3, -0.1),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result1.is_error());
  ASSERT_EQ(result1.error(), GainCurve::kNonIncreasingRangeIllegal);

  auto result2 = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -2.0),
      GainCurve::VolumeMapping(0.1, -0.3),
      GainCurve::VolumeMapping(0.2, -0.3),
      GainCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result2.is_error());
  ASSERT_EQ(result2.error(), GainCurve::kNonIncreasingRangeIllegal);
}

TEST(GainCurveTest, VolumeToDbBasic) {
  auto curve_result = GainCurve::FromMappings({
      GainCurve::VolumeMapping(0.0, -100.0),
      GainCurve::VolumeMapping(1.0, 0.0),
  });

  ASSERT_TRUE(curve_result.is_ok());
  auto curve = curve_result.take_value();

  ASSERT_EQ(curve.VolumeToDb(0.0), -100.0);
  ASSERT_EQ(curve.VolumeToDb(0.25), -75.0);
  ASSERT_EQ(curve.VolumeToDb(0.5), -50.0);
  ASSERT_EQ(curve.VolumeToDb(0.75), -25.0);
  ASSERT_EQ(curve.VolumeToDb(1.0), 0.0);
}

}  // namespace
}  // namespace media::audio
