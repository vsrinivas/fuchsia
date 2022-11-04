// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/v1/loudness_transform.h"

#include <gtest/gtest.h>

#include "src/lib/testing/loop_fixture/test_loop_fixture.h"
#include "src/media/audio/lib/processing/gain.h"

namespace media::audio {
namespace {

TEST(MappedLoudnessTransformTest, VolumesMapped) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(media_audio::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{1.}}), media_audio::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.1}}), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, VolumeValue{0.}}), media_audio::kMinGainDb);
}

TEST(MappedLoudnessTransformTest, GainApplied) {
  const auto volume_curve = VolumeCurve::DefaultForMinGain(media_audio::kMinGainDb);
  auto tf = MappedLoudnessTransform(volume_curve);

  EXPECT_FLOAT_EQ(tf.Evaluate<2>({GainDbFsValue{media_audio::kUnityGainDb},
                                  GainDbFsValue{media_audio::kUnityGainDb}}),
                  media_audio::kUnityGainDb);
  EXPECT_LT(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{-10.}}), media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{media_audio::kMinGainDb}}),
                  media_audio::kMinGainDb);
}

TEST(NoOpLoudnessTransformTest, IsNoOp) {
  auto tf = NoOpLoudnessTransform();

  EXPECT_FLOAT_EQ(tf.Evaluate<2>({GainDbFsValue{media_audio::kUnityGainDb},
                                  GainDbFsValue{media_audio::kUnityGainDb}}),
                  media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{-10.}}),
                  media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{1.}, GainDbFsValue{media_audio::kMinGainDb}}),
                  media_audio::kUnityGainDb);
  EXPECT_FLOAT_EQ(tf.Evaluate<2>({VolumeValue{media_audio::kMinGainDb},
                                  GainDbFsValue{media_audio::kMinGainDb}}),
                  media_audio::kUnityGainDb);
}

}  // namespace
}  // namespace media::audio
