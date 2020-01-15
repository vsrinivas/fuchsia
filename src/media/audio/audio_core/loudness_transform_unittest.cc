// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/loudness_transform.h"

#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

class LoudnessTransformTest : public ::gtest::TestLoopFixture {
 protected:
  LoudnessTransformTest() {}
};

TEST_F(LoudnessTransformTest, VolumesMapped) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{1.}}), Gain::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.1}}), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.}}), Gain::kMinGainDb);
}

TEST_F(LoudnessTransformTest, GainApplied) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(
      tf.Evaluate<2>({GainDbFsValue{Gain::kUnityGainDb}, GainDbFsValue{Gain::kUnityGainDb}}),
      Gain::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{-10.}}), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{Gain::kMinGainDb}}),
                  Gain::kMinGainDb);
}

}  // namespace
}  // namespace media::audio
