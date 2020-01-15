// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/loudness_transform.h"

#include <lib/gtest/test_loop_fixture.h>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

TEST(MappedLoudnessTransformTest, VolumesMapped) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{1.}}), Gain::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.1}}), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.}}), Gain::kMinGainDb);
}

TEST(MappedLoudnessTransformTest, GainApplied) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(Gain::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(
      tf.Evaluate<2>({GainDbFsValue{Gain::kUnityGainDb}, GainDbFsValue{Gain::kUnityGainDb}}),
      Gain::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{-10.}}), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{Gain::kMinGainDb}}),
                  Gain::kMinGainDb);
}

TEST(NoOpLoudnessTransformTest, IsNoOp) {
  auto tf = NoOpLoudnessTransform();

  EXPECT_FLOAT_EQ(
      tf.Evaluate<2>({GainDbFsValue{Gain::kUnityGainDb}, GainDbFsValue{Gain::kUnityGainDb}}),
      Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{-10.}}), Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{Gain::kMinGainDb}}),
                  Gain::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{Gain::kMinGainDb}, GainDbFsValue{Gain::kMinGainDb}}),
                  Gain::kUnityGainDb);
}

}  // namespace
}  // namespace media::audio
