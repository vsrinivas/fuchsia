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

TEST(GeneratorsTest, GenerateCosine_8) {
  auto format = Format::Create<ASF::UNSIGNED_8>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 2, 0.0, 0.0, 0.0);

  // Frequency 0.0 produces constant value. Val 0 is shifted to 0x80.
  EXPECT_EQ(got.samples(), (std::vector<uint8_t>{0x80, 0x80}));
}

TEST(GeneratorsTest, GenerateCosine_16) {
  auto format = Format::Create<ASF::SIGNED_16>(1, 48000).take_value();

  // Frequency of 0.0 produces constant value, with -.4 rounded toward zero.
  auto got = GenerateCosineAudio(format, 2, 0, -32766.4);
  EXPECT_EQ(got.samples(), (std::vector<int16_t>{-32766, -32766}));
}

TEST(GeneratorsTest, GenerateCosine_32) {
  auto format = Format::Create<ASF::SIGNED_24_IN_32>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 4, 1.0, 12345.6, M_PI);

  // PI phase leads to effective magnitude of -12345.6. At frequency 1.0, the change to the buffer
  // is [-12345.6, 0, +12345.6, 0], with +.6 values being rounded away from zero.
  EXPECT_EQ(got.samples(), (std::vector<int32_t>{-12346, 0, 12346, 0}));
}

// Test float-based version of AccumCosine, including default amplitude (1.0)
TEST(GeneratorsTest, GenerateCosine_Float) {
  auto format = Format::Create<ASF::FLOAT>(1, 48000).take_value();
  auto got = GenerateCosineAudio(format, 4, 0);
  EXPECT_THAT(got.samples(), Pointwise(FloatEq(), (std::vector<float>{1.0f, 1.0f, 1.0f, 1.0f})));

  // PI/2 shifts the freq:1 wave left by 1 here.
  got = GenerateCosineAudio(format, 4, 1.0, 0.5, M_PI / 2.0);
  // cos(M_PI/2) is not exactly zero. Translate by 1 so that close-to-zero numbers are rounded
  // to 1.
  for (size_t k = 0; k < 4; k++) {
    got.samples()[k] += 1.0;
  }
  EXPECT_THAT(got.samples(), Pointwise(FloatEq(), (std::vector<float>{1.0f, 0.5f, 1.0f, 1.5f})));
}

TEST(GeneratorsTest, PadToNearestPower2) {
  auto format = Format::Create<ASF::UNSIGNED_8>(1, 48000).take_value();
  auto unpadded = GenerateSequentialAudio(format, 6);
  auto got = PadToNearestPower2(AudioBufferSlice(&unpadded));
  EXPECT_EQ(got.samples(), (std::vector<uint8_t>{0, 1, 2, 3, 4, 5, 0x80, 0x80}));
}

}  // namespace media::audio
