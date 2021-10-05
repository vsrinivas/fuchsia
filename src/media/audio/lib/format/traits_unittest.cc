// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format/traits.h"

#include <cstdint>
#include <type_traits>

#include <gtest/gtest.h>

namespace media::audio {
namespace {

using ASF = fuchsia::media::AudioSampleFormat;

TEST(TraitsTest, Unsigned8) {
  using Traits = SampleFormatTraits<ASF::UNSIGNED_8>;
  EXPECT_TRUE(static_cast<bool>(std::is_same<Traits::SampleT, std::uint8_t>::value));
  EXPECT_EQ(sizeof(Traits::SampleT), sizeof(uint8_t));

  EXPECT_EQ(Traits::kSilentValue, 0x80);
  EXPECT_EQ(Traits::kUnityValue, 0xFF);

  EXPECT_FLOAT_EQ(Traits::ToFloat(0x00), -1.0f);
  EXPECT_GT(Traits::ToFloat(Traits::kUnityValue), 0.99f);

  std::string unity_str = Traits::ToString(Traits::kUnityValue);
  EXPECT_EQ(stoi(unity_str, 0, 16), Traits::kUnityValue);

  EXPECT_EQ(unity_str.length(), Traits::kCharsPerSample);
}

TEST(TraitsTest, Signed16) {
  using Traits = SampleFormatTraits<ASF::SIGNED_16>;
  EXPECT_TRUE(static_cast<bool>(std::is_same<Traits::SampleT, std::int16_t>::value));
  EXPECT_EQ(sizeof(Traits::SampleT), sizeof(int16_t));

  EXPECT_EQ(Traits::kSilentValue, 0x0000);
  EXPECT_EQ(Traits::kUnityValue, 0x7FFF);

  EXPECT_FLOAT_EQ(Traits::ToFloat(0x8000), -1.0f);
  EXPECT_GT(Traits::ToFloat(Traits::kUnityValue), 0.9999f);

  std::string unity_str = Traits::ToString(Traits::kUnityValue);
  EXPECT_EQ(stoi(unity_str, 0, 16), Traits::kUnityValue);

  EXPECT_EQ(unity_str.length(), Traits::kCharsPerSample);
}

TEST(TraitsTest, Signed24) {
  using Traits = SampleFormatTraits<ASF::SIGNED_24_IN_32>;
  EXPECT_TRUE(static_cast<bool>(std::is_same<Traits::SampleT, std::int32_t>::value));
  EXPECT_EQ(sizeof(Traits::SampleT), sizeof(int32_t));

  EXPECT_EQ(Traits::kSilentValue, 0x00000000);
  EXPECT_EQ(Traits::kUnityValue, 0x7FFFFF00);

  EXPECT_FLOAT_EQ(Traits::ToFloat(0x80000000), -1.0f);
  EXPECT_GT(Traits::ToFloat(Traits::kUnityValue), 0.999999f);

  std::string unity_str = Traits::ToString(Traits::kUnityValue);
  EXPECT_EQ(stol(unity_str, 0, 16), Traits::kUnityValue) << Traits::kUnityValue;

  EXPECT_EQ(unity_str.length(), Traits::kCharsPerSample);
}

TEST(TraitsTest, Float32) {
  using Traits = SampleFormatTraits<ASF::FLOAT>;
  EXPECT_TRUE(std::is_floating_point<Traits::SampleT>::value);
  EXPECT_EQ(sizeof(Traits::SampleT), sizeof(float));

  EXPECT_FLOAT_EQ(Traits::kSilentValue, 0.0f);
  EXPECT_FLOAT_EQ(Traits::kUnityValue, 1.0f);

  EXPECT_FLOAT_EQ(Traits::ToFloat(-1.0f), -1.0f);
  EXPECT_FLOAT_EQ(Traits::ToFloat(Traits::kUnityValue), 1.0f);

  std::string unity_str = Traits::ToString(Traits::kUnityValue);
  EXPECT_FLOAT_EQ(stof(unity_str), Traits::kUnityValue);

  EXPECT_EQ(unity_str.length(), Traits::kCharsPerSample);
}

}  // namespace
}  // namespace media::audio
