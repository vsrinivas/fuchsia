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

// Multiple mono AudioBufferSlice can be interleaved to an AudioBuffer
TEST(AudioBufferTest, Interleave) {
  constexpr uint32_t kFrameRate = 32000;
  const Format kFormat1 = Format::Create(fuchsia::media::AudioStreamType{
                                             .sample_format = kSampleFormat,
                                             .channels = 1,
                                             .frames_per_second = kFrameRate,
                                         })
                              .take_value();

  // Mono 20 frames, with values 0..19
  AudioBuffer<kSampleFormat> buffer(kFormat1, 20);
  for (auto frame = 0u; frame < 20; ++frame) {
    buffer.samples()[frame] = frame;
  }

  // Slice #0 has vals 0..3; #1 has 4..7; #2 8..11; #3 12..15; #4 16..19.
  auto slices = std::vector<AudioBufferSlice<kSampleFormat>>();
  slices.push_back(AudioBufferSlice(&buffer, 0, 4));
  slices.push_back(AudioBufferSlice(&buffer, 4, 8));
  slices.push_back(AudioBufferSlice(&buffer, 8, 12));
  slices.push_back(AudioBufferSlice(&buffer, 12, 16));
  slices.push_back(AudioBufferSlice(&buffer, 16, 20));

  // Interleave these five slices into a 5-channel file.
  auto interleaved = AudioBuffer<kSampleFormat>::Interleave(slices);
  EXPECT_EQ(interleaved.format().channels(), 5u);

  // All characteristics except channels must match the original slices.
  EXPECT_EQ(interleaved.NumFrames(), 4u);
  EXPECT_EQ(interleaved.format().frames_per_second(), kFrameRate);
  EXPECT_EQ(interleaved.format().sample_format(), kSampleFormat);

  // In resulting buffer, first frame has values [0,4,8,12,16], second frame [1,5,9,13,17], etc.
  for (auto frame = 0u; frame < interleaved.NumFrames(); ++frame) {
    // Within a frame, values should increase by 4 with each successive channel.
    EXPECT_EQ(interleaved.SampleAt(frame, 0), static_cast<int16_t>(frame));
    EXPECT_EQ(interleaved.SampleAt(frame, 1), interleaved.SampleAt(frame, 0) + 4);
    EXPECT_EQ(interleaved.SampleAt(frame, 2), interleaved.SampleAt(frame, 1) + 4);
    EXPECT_EQ(interleaved.SampleAt(frame, 3), interleaved.SampleAt(frame, 2) + 4);
    EXPECT_EQ(interleaved.SampleAt(frame, 4), interleaved.SampleAt(frame, 3) + 4);
  }
}

}  // namespace media::audio
