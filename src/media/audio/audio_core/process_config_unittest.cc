// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/process_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/testing/matchers.h"

using media::audio::testing::VolumeMappingEq;
using testing::FloatEq;
using testing::Matcher;
using testing::Pointwise;

namespace media::audio {
namespace {

TEST(ProcessConfigTest, Build) {
  auto volume_curve = VolumeCurve::DefaultForMinGain(-160.0f);
  auto config = ProcessConfig::Builder().SetDefaultVolumeCurve(volume_curve).Build();

  EXPECT_THAT(config.default_volume_curve().mappings(),
              Pointwise(VolumeMappingEq(), volume_curve.mappings()));
}

TEST(ProcessConfigTest, LoudnessTransform) {
  auto volume_curve = VolumeCurve::DefaultForMinGain(-160.0f);
  auto config = ProcessConfig::Builder().SetDefaultVolumeCurve(volume_curve).Build();

  auto transform = config.default_loudness_transform();
  EXPECT_NE(transform, nullptr);
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{0.}}), Gain::kMinGainDb);
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{1.}}), Gain::kUnityGainDb);
}

TEST(ProcessConfigTest, SetInstance) {
  auto volume_curve = VolumeCurve::DefaultForMinGain(-160.0f);
  auto config = ProcessConfig::Builder().SetDefaultVolumeCurve(volume_curve).Build();

  // Test that |set_instance|/|instance| are coherent and we can reset the instance once a handle
  // goes out of scope.
  {
    auto handle = ProcessConfig::set_instance(config);
    EXPECT_THAT(
        config.default_volume_curve().mappings(),
        Pointwise(VolumeMappingEq(), ProcessConfig::instance().default_volume_curve().mappings()));
  }
  {
    auto handle = ProcessConfig::set_instance(config);
    EXPECT_THAT(
        config.default_volume_curve().mappings(),
        Pointwise(VolumeMappingEq(), ProcessConfig::instance().default_volume_curve().mappings()));
  }
}

}  // namespace
}  // namespace media::audio
