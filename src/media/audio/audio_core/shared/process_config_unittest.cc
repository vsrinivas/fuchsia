// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/process_config.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "src/media/audio/audio_core/shared/testing/matchers.h"
#include "src/media/audio/lib/processing/gain.h"

using media::audio::testing::VolumeMappingEq;
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
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{0.}}), media_audio::kMinGainDb);
  EXPECT_FLOAT_EQ(transform->Evaluate<1>({VolumeValue{1.}}), media_audio::kUnityGainDb);
}

TEST(ProcessConfigTest, CanCopy) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(-160.0f);
  const ProcessConfig config = ProcessConfig::Builder().SetDefaultVolumeCurve(volume_curve).Build();

  const ProcessConfig config_copy = config;
  config_copy.default_loudness_transform()->Evaluate<1>({VolumeValue{1}});
}

}  // namespace
}  // namespace media::audio
