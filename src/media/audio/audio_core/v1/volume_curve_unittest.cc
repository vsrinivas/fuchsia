// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/volume_curve.h"

#include <fuchsia/media/cpp/fidl.h>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/lib/processing/gain.h"

static constexpr auto MIN_VOLUME = fuchsia::media::audio::MIN_VOLUME;
static constexpr auto MAX_VOLUME = fuchsia::media::audio::MAX_VOLUME;
static constexpr auto MUTED_GAIN_DB = fuchsia::media::audio::MUTED_GAIN_DB;

namespace media::audio {
namespace {

TEST(VolumeCurveTest, ValidationRejectsEmpty) {
  auto result = VolumeCurve::FromMappings({});
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsOneMapping) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsNoMinVolume) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(0.2, -0.45),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsNoMaxVolume) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.5, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsWrongGainForMinVolume) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB + 1),
      VolumeCurve::VolumeMapping(MAX_VOLUME, 0),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsWrongGainForMaxVolume) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(MAX_VOLUME, 1.0),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsDuplicateVolumes) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2, -34.0),
      VolumeCurve::VolumeMapping(0.2, -31.0),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsVolumesNotIncreasing) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2, -34.0),
      VolumeCurve::VolumeMapping(0.1, -31.0),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsDuplicateGains) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2, -0.3),
      VolumeCurve::VolumeMapping(0.3, -0.3),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsGainsNotIncreasing) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2, -1.0),
      VolumeCurve::VolumeMapping(0.3, -10.0),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, VolumeToDbBasic) {
  auto curve_result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(FLT_EPSILON, -100.0),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });

  ASSERT_TRUE(curve_result.is_ok());
  auto curve = curve_result.take_value();

  EXPECT_FLOAT_EQ(curve.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(FLT_EPSILON), -100.0);
  EXPECT_FLOAT_EQ(curve.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-100.0), FLT_EPSILON);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.25), -75.0);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-75.0), 0.25);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.5), -50.0);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-50.0), 0.5);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.75), -25.0);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-25.0), 0.75);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);
}

TEST(VolumeCurveTest, DefaultCurveWithMinGainDb) {
  auto curve100 = VolumeCurve::DefaultForMinGain(-100.0);
  auto curve50 = VolumeCurve::DefaultForMinGain(-50.0);

  EXPECT_FLOAT_EQ(curve100.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve100.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);

  EXPECT_FLOAT_EQ(curve50.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve50.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);

  EXPECT_FLOAT_EQ(curve100.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve100.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);

  EXPECT_FLOAT_EQ(curve50.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve50.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);

  const auto middle100 = curve100.VolumeToDb(0.5);
  const auto middle50 = curve50.VolumeToDb(0.5);

  EXPECT_LT(middle100, middle50);
}

TEST(VolumeCurveTest, DefaultCurveWithMuteGainDoesNotAbort) {
  VolumeCurve::DefaultForMinGain(MUTED_GAIN_DB);
}

TEST(VolumeCurveTest, Interpolate) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(0.0, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.5, -10.0),
      VolumeCurve::VolumeMapping(1.0, 0.0),
  });
  ASSERT_TRUE(result.is_ok());
  auto curve = result.take_value();

  EXPECT_FLOAT_EQ((MUTED_GAIN_DB - 10.0) / 2, curve.VolumeToDb(0.25f));
  EXPECT_FLOAT_EQ((-10.0 - 0.0) / 2, curve.VolumeToDb(0.75f));

  EXPECT_FLOAT_EQ(0.25f, curve.DbToVolume((MUTED_GAIN_DB - 10.0) / 2));
  EXPECT_FLOAT_EQ(0.75f, curve.DbToVolume((-10.0 - 0.0) / 2));
}

}  // namespace
}  // namespace media::audio
