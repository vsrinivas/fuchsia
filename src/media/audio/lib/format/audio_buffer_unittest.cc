// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format/audio_buffer.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace media::audio {

TEST(AudioBufferTest, Basics) {
  constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
  Format format = Format::Create(fuchsia::media::AudioStreamType{
                                     .sample_format = kSampleFormat,
                                     .channels = 2,
                                     .frames_per_second = 48000,
                                 })
                      .take_value();

  AudioBuffer<kSampleFormat> buf(format, 10);
  EXPECT_EQ(10U, buf.NumFrames());
  EXPECT_EQ(10U * 2 * sizeof(int16_t), buf.NumBytes());
  EXPECT_EQ(0U, buf.SampleIndex(0, 0));
  EXPECT_EQ(1U, buf.SampleIndex(0, 1));
  EXPECT_EQ(2U, buf.SampleIndex(1, 0));

  AudioBufferSlice slice1(&buf);
  EXPECT_EQ(10U, slice1.NumFrames());
  EXPECT_EQ(0U, slice1.SampleIndex(0, 0));
  EXPECT_EQ(3U, slice1.SampleIndex(1, 1));

  AudioBufferSlice slice2(&buf, 5, 8);
  EXPECT_EQ(3U, slice2.NumFrames());
  EXPECT_EQ(10U, slice2.SampleIndex(0, 0));
  EXPECT_EQ(13U, slice2.SampleIndex(1, 1));
}

}  // namespace media::audio
