// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/device_config.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {
namespace {

const auto kVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);
const auto kConfig = ProcessConfig::Builder().SetDefaultVolumeCurve(kVolumeCurve).Build();

TEST(OutputDeviceProfileTest, TransformForDependentVolumeControl) {
  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto dependent_volume_tf =
      DeviceConfig::OutputDeviceProfile(eligible_for_loopback, /* usage_support_set */ {},
                                        /*independent_volume_control=*/false,
                                        /*pipeline_config=*/PipelineConfig::Default(),
                                        /*driver_gain_db=*/0.0,
                                        /*volume_curve=*/kVolumeCurve)
          .loudness_transform();

  EXPECT_FLOAT_EQ(dependent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}),
                  default_tf->Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}));
  EXPECT_FLOAT_EQ(dependent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}),
                  default_tf->Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}));
}

TEST(OutputDeviceProfileTest, TransformForIndependentVolumeControl) {
  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto independent_volume_tf =
      DeviceConfig::OutputDeviceProfile(eligible_for_loopback, /* usage_support_set */ {},
                                        /*independent_volume_control=*/true)
          .loudness_transform();

  EXPECT_NE(independent_volume_tf, default_tf);

  const auto no_op_tf = NoOpLoudnessTransform();
  EXPECT_FLOAT_EQ(independent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}),
                  no_op_tf.Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}));

  EXPECT_FLOAT_EQ(independent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}),
                  no_op_tf.Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}));
}

TEST(DeviceProfileTest, DeviceProfileTransform) {
  const auto handle = ProcessConfig::set_instance(kConfig);

  const auto default_tf = kConfig.default_loudness_transform();
  EXPECT_EQ(DeviceConfig::DeviceProfile(/* usage_support_set */ {}).loudness_transform(),
            default_tf);
}

}  // namespace
}  // namespace media::audio
