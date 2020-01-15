// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/routing_config.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/process_config.h"

namespace media::audio {
namespace {

const auto kVolumeCurve = VolumeCurve::DefaultForMinGain(-160.0f);
const auto kConfig = ProcessConfig::Builder().SetDefaultVolumeCurve(kVolumeCurve).Build();

TEST(DeviceProfileTest, TransformForDependentVolumeControl) {
  const auto handle = ProcessConfig::set_instance(kConfig);

  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto usage_support_set = RoutingConfig::UsageSupportSet{};
  EXPECT_EQ(RoutingConfig::DeviceProfile(eligible_for_loopback, usage_support_set,
                                         /*independent_volume_control=*/false)
                .loudness_transform(),
            default_tf);
}

TEST(DeviceProfileTest, TransformForIndependentVolumeControl) {
  const auto handle = ProcessConfig::set_instance(kConfig);

  const auto default_tf = kConfig.default_loudness_transform();

  const auto eligible_for_loopback = false;
  const auto usage_support_set = RoutingConfig::UsageSupportSet{};

  const auto independent_volume_tf =
      RoutingConfig::DeviceProfile(eligible_for_loopback, usage_support_set,
                                   /*independent_volume_control=*/true)
          .loudness_transform();

  EXPECT_NE(independent_volume_tf, default_tf);

  const auto no_op_tf = NoOpLoudnessTransform();
  EXPECT_FLOAT_EQ(independent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}),
                  no_op_tf.Evaluate<1>({GainDbFsValue{Gain::kMinGainDb}}));

  EXPECT_FLOAT_EQ(independent_volume_tf->Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}),
                  no_op_tf.Evaluate<1>({GainDbFsValue{Gain::kMaxGainDb}}));
}

}  // namespace
}  // namespace media::audio
