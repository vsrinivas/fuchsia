// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/point_sampler.h"

#include <fbl/algorithm.h>
#include <gtest/gtest.h>

namespace media::audio::mixer {
namespace {

std::unique_ptr<Mixer> SelectPointSampler(
    uint32_t source_channels, uint32_t source_frame_rate,
    fuchsia::media::AudioSampleFormat source_format, uint32_t dest_channels,
    uint32_t dest_frame_rate,
    fuchsia::media::AudioSampleFormat dest_format = fuchsia::media::AudioSampleFormat::FLOAT) {
  fuchsia::media::AudioStreamType source_stream_type;
  source_stream_type.channels = source_channels;
  source_stream_type.frames_per_second = source_frame_rate;
  source_stream_type.sample_format = source_format;

  fuchsia::media::AudioStreamType dest_stream_type;
  dest_stream_type.channels = dest_channels;
  dest_stream_type.frames_per_second = dest_frame_rate;
  dest_stream_type.sample_format = dest_format;

  return mixer::PointSampler::Select(source_stream_type, dest_stream_type);
}

TEST(PointSamplerTest, Construction) {
  //
  // These formats are supported
  auto mixer =
      SelectPointSampler(1, 48000, fuchsia::media::AudioSampleFormat::UNSIGNED_8, 1, 96000);
  EXPECT_NE(mixer, nullptr);

  mixer = SelectPointSampler(2, 44100, fuchsia::media::AudioSampleFormat::SIGNED_16, 2, 48000);
  EXPECT_NE(mixer, nullptr);

  mixer =
      SelectPointSampler(2, 24000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1, 22050);
  EXPECT_NE(mixer, nullptr);

  mixer = SelectPointSampler(1, 48000, fuchsia::media::AudioSampleFormat::FLOAT, 1, 48000);
  EXPECT_NE(mixer, nullptr);

  //
  // These formats are not supported
  mixer =
      SelectPointSampler(5, 24000, fuchsia::media::AudioSampleFormat::SIGNED_24_IN_32, 1, 22050);
  EXPECT_EQ(mixer, nullptr);

  mixer = SelectPointSampler(1, 48000, fuchsia::media::AudioSampleFormat::FLOAT, 9, 96000);
  EXPECT_EQ(mixer, nullptr);

  mixer = SelectPointSampler(4, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16, 3, 48000);
  EXPECT_EQ(mixer, nullptr);

  mixer = SelectPointSampler(3, 48000, fuchsia::media::AudioSampleFormat::SIGNED_16, 4, 48000);
  EXPECT_EQ(mixer, nullptr);
}

}  // namespace
}  // namespace media::audio::mixer
