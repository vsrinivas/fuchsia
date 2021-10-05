// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/analysis/generators.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using testing::FloatEq;
using testing::Pointwise;
using ASF = fuchsia::media::AudioSampleFormat;

namespace media::audio {

// If unspecified, GenerateCosineAudio magnitude is "max" and phase is 0.

// Test default amplitude and phase for uint8: 0xFF (127 after being shifted) and 0x01 (-127)
// For each of these cases we generate a buffer with length 2 and (in-buffer) frequency 1.0
TEST(GeneratorsTest, GenerateCosine8Default) {
  auto format = Format::Create<ASF::UNSIGNED_8>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 2, 1.0);
  EXPECT_EQ(got.samples(), (std::vector<uint8_t>{0xFF, 0x01}));
}

// Test default amplitude and phase for int16: 0x7FFF (32767) and 0
TEST(GeneratorsTest, GenerateCosine16Default) {
  auto format = Format::Create<ASF::SIGNED_16>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 2, 1.0);
  EXPECT_EQ(got.samples(), (std::vector<int16_t>{0x7FFF, -0x7FFF}));
}

// Test default amplitude and phase for int24-in-32: 0x7FFFFF00 and 0
TEST(GeneratorsTest, GenerateCosine24Default) {
  auto format = Format::Create<ASF::SIGNED_24_IN_32>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 2, 1.0);
  EXPECT_EQ(got.samples(), (std::vector<int32_t>{0x7FFFFF00, -0x7FFFFF00}));
}

// Test default amplitude and phase for float32: 1.0 and 0
TEST(GeneratorsTest, GenerateCosineFloatDefault) {
  auto format = Format::Create<ASF::FLOAT>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 2, 1.0);
  EXPECT_THAT(got.samples(), Pointwise(FloatEq(), (std::vector<float>{1.0f, -1.0f})));
}

TEST(GeneratorsTest, GenerateCosine8) {
  auto format = Format::Create<ASF::UNSIGNED_8>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 4, 1.0, 16.0);

  // Frequency 1.0 produces a four-frame cycle. uint8 values are shifted by 0x80.
  EXPECT_EQ(got.samples(), (std::vector<uint8_t>{0x90, 0x80, 0x70, 0x80}));
}

TEST(GeneratorsTest, GenerateCosine16) {
  auto format = Format::Create<ASF::SIGNED_16>(1, 48000).take_value();

  // Frequency of 0.0 produces constant value, with -.4 rounded toward zero.
  auto got = GenerateCosineAudio(format, 2, 0, -32766.4);
  EXPECT_EQ(got.samples(), (std::vector<int16_t>{-32766, -32766}));
}

// For 24-in-32, all output must be a multiple of 256 (bottom byte is 0)
TEST(GeneratorsTest, GenerateCosine24) {
  auto format = Format::Create<ASF::SIGNED_24_IN_32>(1, 48000).take_value();

  // PI phase with invert the output, so we expect [-12345.6, 0, +12345.6, 0].
  // Output must be a multiple of 256, and 12345.6 is closer to 12288 than 12544.
  auto got = GenerateCosineAudio(format, 4, 1.0, 12345.6, M_PI);
  EXPECT_EQ(got.samples(), (std::vector<int32_t>{-12288, 0, 12288, 0}));

  // PI/2 phase shifts the signal by one frame. We expect [0, -12416, 0, 12416]. Value 12416 is
  // exactly midway between multiples of 256, so we expect to round out (away from zero).
  got = GenerateCosineAudio(format, 4, 1.0, 12416);
  EXPECT_EQ(got.samples(), (std::vector<int32_t>{12544, 0, -12544, 0}));
}

// Test float-based version of AccumCosine
TEST(GeneratorsTest, GenerateCosineFloat) {
  auto format = Format::Create<ASF::FLOAT>(1, 48000).take_value();

  // Frequency 2.0 produces alternating value. PI phase inverts the cosine output.
  auto got = GenerateCosineAudio(format, 4, 2.0, 1.0, M_PI);
  EXPECT_THAT(got.samples(), Pointwise(FloatEq(), (std::vector<float>{-1.0f, 1.0f, -1.0f, 1.0f})));
}

TEST(GeneratorsTest, PadToNearestPower2) {
  auto format = Format::Create<ASF::UNSIGNED_8>(1, 48000).take_value();
  auto unpadded = GenerateSequentialAudio(format, 6);
  auto got = PadToNearestPower2(AudioBufferSlice(&unpadded));
  EXPECT_EQ(got.samples(), (std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 0x80, 0x80}));
}

}  // namespace media::audio
