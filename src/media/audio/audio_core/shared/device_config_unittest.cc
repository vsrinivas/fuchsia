// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/shared/device_config.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/shared/process_config.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {
namespace {

const auto kVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);
const auto kConfig = ProcessConfig::Builder().SetDefaultVolumeCurve(kVolumeCurve).Build();

TEST(OutputDeviceProfileTest, TransformForDependentVolumeControl) {
  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto dependent_volume_tf =
      DeviceConfig::OutputDeviceProfile(eligible_for_loopback, /*supported_usages=*/{},
                                        kVolumeCurve, /*independent_volume_control=*/false,
                                        /*pipeline_config=*/PipelineConfig::Default(),
                                        /*driver_gain_db=*/0.0, /*software_gain_db=*/0.0)
          .loudness_transform();

  EXPECT_FLOAT_EQ(
      dependent_volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}),
      default_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}));
  EXPECT_FLOAT_EQ(
      dependent_volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}),
      default_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}));
}

TEST(OutputDeviceProfileTest, TransformForIndependentVolumeControl) {
  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto independent_volume_tf =
      DeviceConfig::OutputDeviceProfile(eligible_for_loopback, /*supported_usages=*/{},
                                        kVolumeCurve, /*independent_volume_control=*/true,
                                        PipelineConfig::Default(), /*driver_gain_db=*/0.0,
                                        /*software_gain_db=*/0.0)
          .loudness_transform();

  EXPECT_NE(independent_volume_tf, default_tf);

  const auto no_op_tf = NoOpLoudnessTransform();
  EXPECT_FLOAT_EQ(
      independent_volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}),
      no_op_tf.Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}));

  EXPECT_FLOAT_EQ(
      independent_volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}),
      no_op_tf.Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}));
}

TEST(DeviceProfileTest, DeviceProfileTransform) {
  const auto default_tf = kConfig.default_loudness_transform();
  const auto volume_tf =
      DeviceConfig::DeviceProfile(/*supported_usages=*/{}, kVolumeCurve,
                                  /*driver_gain_db=*/0.0, /*software_gain_db=*/0.0)
          .loudness_transform();

  EXPECT_FLOAT_EQ(volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}),
                  default_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MUTED_GAIN_DB}}));
  EXPECT_FLOAT_EQ(volume_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}),
                  default_tf->Evaluate<1>({GainDbFsValue{fuchsia::media::audio::MAX_GAIN_DB}}));
}

}  // namespace
}  // namespace media::audio
