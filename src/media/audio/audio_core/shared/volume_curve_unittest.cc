// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/volume_curve.h"

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
      VolumeCurve::VolumeMapping(0.2f, -0.45f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsNoMaxVolume) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.5f, media_audio::kUnityGainDb),
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
      VolumeCurve::VolumeMapping(0.2f, -34.0f),
      VolumeCurve::VolumeMapping(0.2f, -31.0f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsVolumesNotIncreasing) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2f, -34.0f),
      VolumeCurve::VolumeMapping(0.1f, -31.0f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsDuplicateGains) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2f, -0.3f),
      VolumeCurve::VolumeMapping(0.3f, -0.3f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, ValidationRejectsGainsNotIncreasing) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.2f, -1.0f),
      VolumeCurve::VolumeMapping(0.3f, -10.0f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });
  ASSERT_TRUE(result.is_error());
}

TEST(VolumeCurveTest, VolumeToDbBasic) {
  auto curve_result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(MIN_VOLUME, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(FLT_EPSILON, -100.0f),
      VolumeCurve::VolumeMapping(MAX_VOLUME, media_audio::kUnityGainDb),
  });

  ASSERT_TRUE(curve_result.is_ok());
  auto curve = curve_result.take_value();

  EXPECT_FLOAT_EQ(curve.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve.VolumeToDb(FLT_EPSILON), -100.0f);
  EXPECT_FLOAT_EQ(curve.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-100.0f), FLT_EPSILON);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.25f), -75.0f);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-75.0f), 0.25);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.5f), -50.0f);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-50.0f), 0.5f);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(0.75f), -25.0f);
  EXPECT_FLOAT_EQ(curve.DbToVolume(-25.0f), 0.75f);

  EXPECT_FLOAT_EQ(curve.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);
}

TEST(VolumeCurveTest, DefaultCurveWithMinGainDb) {
  auto curve100 = VolumeCurve::DefaultForMinGain(-100.0f);
  auto curve50 = VolumeCurve::DefaultForMinGain(-50.0f);

  EXPECT_FLOAT_EQ(curve100.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve100.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);

  EXPECT_FLOAT_EQ(curve50.VolumeToDb(MIN_VOLUME), MUTED_GAIN_DB);
  EXPECT_FLOAT_EQ(curve50.DbToVolume(MUTED_GAIN_DB), MIN_VOLUME);

  EXPECT_FLOAT_EQ(curve100.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve100.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);

  EXPECT_FLOAT_EQ(curve50.VolumeToDb(MAX_VOLUME), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(curve50.DbToVolume(media_audio::kUnityGainDb), MAX_VOLUME);

  const auto middle100 = curve100.VolumeToDb(0.5f);
  const auto middle50 = curve50.VolumeToDb(0.5f);

  EXPECT_LT(middle100, middle50);
}

TEST(VolumeCurveTest, DefaultCurveWithMuteGainDoesNotAbort) {
  VolumeCurve::DefaultForMinGain(MUTED_GAIN_DB);
}

TEST(VolumeCurveTest, Interpolate) {
  auto result = VolumeCurve::FromMappings({
      VolumeCurve::VolumeMapping(0.0f, MUTED_GAIN_DB),
      VolumeCurve::VolumeMapping(0.5f, -10.0f),
      VolumeCurve::VolumeMapping(1.0f, 0.0f),
  });
  ASSERT_TRUE(result.is_ok());
  auto curve = result.take_value();

  EXPECT_FLOAT_EQ((MUTED_GAIN_DB - 10.0f) / 2, curve.VolumeToDb(0.25f));
  EXPECT_FLOAT_EQ((-10.0f - 0.0f) / 2, curve.VolumeToDb(0.75f));

  EXPECT_FLOAT_EQ(0.25f, curve.DbToVolume((MUTED_GAIN_DB - 10.0f) / 2));
  EXPECT_FLOAT_EQ(0.75f, curve.DbToVolume((-10.0f - 0.0f) / 2));
}

}  // namespace
}  // namespace media::audio
