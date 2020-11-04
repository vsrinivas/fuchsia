// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format/audio_buffer.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "fuchsia/media/cpp/fidl.h"

namespace media::audio {

constexpr auto kSampleFormat = fuchsia::media::AudioSampleFormat::SIGNED_16;
static const Format kFormat = Format::Create(fuchsia::media::AudioStreamType{
                                                 .sample_format = kSampleFormat,
                                                 .channels = 2,
                                                 .frames_per_second = 48000,
                                             })
                                  .take_value();

TEST(AudioBufferTest, Basics) {
  AudioBuffer<kSampleFormat> buffer(kFormat, 10);
  EXPECT_EQ(buffer.NumFrames(), 10u);
  EXPECT_EQ(buffer.NumSamples(), buffer.NumFrames() * 2);
  EXPECT_EQ(buffer.NumBytes(), buffer.NumSamples() * sizeof(int16_t));

  AudioBufferSlice slice1(&buffer);
  EXPECT_EQ(slice1.NumFrames(), buffer.NumFrames());
  EXPECT_EQ(slice1.NumSamples(), buffer.NumSamples());
  EXPECT_EQ(slice1.NumBytes(), buffer.NumBytes());

  AudioBufferSlice slice2(&buffer, 5, 8);
  EXPECT_EQ(slice2.NumFrames(), 3u);
  EXPECT_EQ(slice2.NumSamples(), 6u);
  EXPECT_EQ(slice2.NumBytes(), 12u);
}

// Verify Buffer::samples (lvalue and rvalue), and SampleIndex() / SampleAt() for Buffer and Slice
TEST(AudioBufferTest, SampleAccess) {
  AudioBuffer<kSampleFormat> buffer(kFormat, 10);
  buffer.samples()[0] = 10000;
  buffer.samples()[1] = 11;
  buffer.samples()[10] = 222;
  buffer.samples()[15] = 3333;

  EXPECT_EQ(buffer.SampleIndex(0, 0), 0u);
  EXPECT_EQ(buffer.SampleIndex(0, 1), 1u);
  EXPECT_EQ(buffer.SampleIndex(5, 0), 10u);
  EXPECT_EQ(buffer.SampleIndex(7, 1), 15u);

  EXPECT_EQ(buffer.SampleAt(0, 0), 10000);
  EXPECT_EQ(buffer.SampleAt(0, 1), 11);
  EXPECT_EQ(buffer.SampleAt(5, 0), 222);
  EXPECT_EQ(buffer.SampleAt(7, 1), 3333);

  AudioBufferSlice slice(&buffer);
  EXPECT_EQ(slice.SampleIndex(0, 0), buffer.SampleIndex(0, 0));
  EXPECT_EQ(slice.SampleIndex(7, 1), buffer.SampleIndex(7, 1));

  EXPECT_EQ(slice.SampleAt(0, 0), buffer.samples()[0]);
  EXPECT_EQ(slice.SampleAt(5, 0), buffer.samples()[10]);
  EXPECT_EQ(slice.SampleAt(0, 1), buffer.SampleAt(0, 1));
  EXPECT_EQ(slice.SampleAt(7, 1), buffer.SampleAt(7, 1));

  AudioBufferSlice slice2(&buffer, 5, 8);
  EXPECT_EQ(slice2.SampleIndex(0, 1), 11u);
  EXPECT_EQ(slice2.SampleIndex(2, 0), 14u);

  EXPECT_EQ(slice2.SampleAt(0, 0), buffer.SampleAt(5, 0));
  EXPECT_EQ(slice2.SampleAt(2, 1), slice.SampleAt(7, 1));

  EXPECT_EQ(*(slice2.begin()), buffer.SampleAt(5, 0));
  EXPECT_EQ(*(slice2.end() - 1), slice.SampleAt(7, 1));
}

// An AudioBufferSlice can be appended to an AudioBuffer
TEST(AudioBufferTest, AppendSlice) {
  // 2 frames with 2 channels per frame, so buffer has samples [0..3]
  AudioBuffer<kSampleFormat> buffer(kFormat, 2);
  buffer.samples()[2] = 2345;

  // buffer2 has 3 frames, thus samples [0..5]
  AudioBuffer<kSampleFormat> buffer2(kFormat, 3);
  buffer2.samples()[3] = 3333;

  // slice contains only buffer2[2..5]
  AudioBufferSlice slice2(&buffer2, 1, 3);

  // Appending slice[] will copy buffer2[2..5] to buffer[4..7]
  buffer.Append(slice2);
  EXPECT_EQ(buffer.NumFrames(), 4u);
  EXPECT_EQ(buffer.NumSamples(), 8u);
  EXPECT_EQ(buffer.NumBytes(), 16u);
  EXPECT_EQ(buffer.samples()[2], 2345);
  EXPECT_EQ(buffer.samples()[5], 3333);

  // buffer2 and slice2 should be unchanged
  EXPECT_EQ(buffer2.samples()[3], 3333);
  EXPECT_EQ(buffer2.NumFrames(), 3u);
  EXPECT_EQ(buffer2.NumSamples(), 6u);
  EXPECT_EQ(buffer2.NumBytes(), 12u);

  EXPECT_EQ(slice2.SampleAt(0, 1), 3333);
  EXPECT_EQ(slice2.NumFrames(), 2u);
  EXPECT_EQ(slice2.NumSamples(), 4u);
  EXPECT_EQ(slice2.NumBytes(), 8u);
}

}  // namespace media::audio
