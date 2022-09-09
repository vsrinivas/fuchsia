// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/lib/format2/sample_converter.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

namespace media_audio {
namespace {

TEST(SampleConverterTest, Unsigned8) {
  const std::vector<uint8_t> unsigned8_samples = {0x00, 0x40, 0x80, 0xE0};
  const std::vector<float> float_samples = {-1.0f, -0.5f, 0.0f, 0.75f};

  for (int i = 0; i < static_cast<int>(unsigned8_samples.size()); ++i) {
    const uint8_t unsigned8_sample = unsigned8_samples[i];
    const float float_sample = float_samples[i];

    EXPECT_EQ(SampleConverter<uint8_t>::FromFloat(float_sample), unsigned8_sample);
    EXPECT_FLOAT_EQ(SampleConverter<uint8_t>::ToFloat(unsigned8_sample), float_sample);

    // Back and forth conversions.
    EXPECT_EQ(
        SampleConverter<uint8_t>::FromFloat(SampleConverter<uint8_t>::ToFloat(unsigned8_sample)),
        unsigned8_sample);
    EXPECT_FLOAT_EQ(
        SampleConverter<uint8_t>::ToFloat(SampleConverter<uint8_t>::FromFloat(float_sample)),
        float_sample);
  }

  // Should be normalized.
  EXPECT_EQ(SampleConverter<uint8_t>::FromFloat(5.5f), 0xFF);
  EXPECT_EQ(SampleConverter<uint8_t>::FromFloat(-1.1f), 0x00);

  EXPECT_LT(SampleConverter<uint8_t>::ToFloat(0xFF), 1.0f);
  EXPECT_GT(SampleConverter<uint8_t>::ToFloat(0xFF), 0.99f);
}

TEST(SampleConverterTest, Signed16) {
  const std::vector<int16_t> signed16_samples = {kMinInt16, -0x4000, 0, 0x6000};
  const std::vector<float> float_samples = {-1.0f, -0.5f, 0.0f, 0.75f};

  for (int i = 0; i < static_cast<int>(signed16_samples.size()); ++i) {
    const int16_t signed16_sample = signed16_samples[i];
    const float float_sample = float_samples[i];

    EXPECT_EQ(SampleConverter<int16_t>::FromFloat(float_sample), signed16_sample);
    EXPECT_FLOAT_EQ(SampleConverter<int16_t>::ToFloat(signed16_sample), float_sample);

    // Back and forth conversions.
    EXPECT_EQ(
        SampleConverter<int16_t>::FromFloat(SampleConverter<int16_t>::ToFloat(signed16_sample)),
        signed16_sample);
    EXPECT_FLOAT_EQ(
        SampleConverter<int16_t>::ToFloat(SampleConverter<int16_t>::FromFloat(float_sample)),
        float_sample);
  }

  // Should be normalized.
  EXPECT_EQ(SampleConverter<int16_t>::FromFloat(5.5f), kMaxInt16);
  EXPECT_EQ(SampleConverter<int16_t>::FromFloat(-1.1f), kMinInt16);

  EXPECT_LT(SampleConverter<int16_t>::ToFloat(kMaxInt16), 1.0f);
  EXPECT_GT(SampleConverter<int16_t>::ToFloat(kMaxInt16), 0.9999f);
}

TEST(SampleConverterTest, Signed24In32) {
  const std::vector<int32_t> signed24in32_samples = {kMinInt24In32, -0x40000000, 0, 0x60000000};
  const std::vector<float> float_samples = {-1.0f, -0.5f, 0.0f, 0.75f};

  for (int i = 0; i < static_cast<int>(signed24in32_samples.size()); ++i) {
    const int32_t signed24in32_sample = signed24in32_samples[i];
    const float float_sample = float_samples[i];

    EXPECT_EQ(SampleConverter<int32_t>::FromFloat(float_sample), signed24in32_sample);
    EXPECT_FLOAT_EQ(SampleConverter<int32_t>::ToFloat(signed24in32_sample), float_sample);

    // Back and forth conversions.
    EXPECT_EQ(
        SampleConverter<int32_t>::FromFloat(SampleConverter<int32_t>::ToFloat(signed24in32_sample)),
        signed24in32_sample);
    EXPECT_FLOAT_EQ(
        SampleConverter<int32_t>::ToFloat(SampleConverter<int32_t>::FromFloat(float_sample)),
        float_sample);
  }

  // Should be normalized.
  EXPECT_EQ(SampleConverter<int32_t>::FromFloat(5.5f), kMaxInt24In32);
  EXPECT_EQ(SampleConverter<int32_t>::FromFloat(-1.1f), kMinInt24In32);

  EXPECT_LT(SampleConverter<int32_t>::ToFloat(kMaxInt24In32), 1.0f);
  EXPECT_GT(SampleConverter<int32_t>::ToFloat(kMaxInt24In32), 0.999999f);
}

TEST(SampleConverterTest, Float) {
  const std::vector<float> samples = {-1.0f, -0.5f, 0.0f, 0.75f, 1.0f};

  for (const float sample : samples) {
    EXPECT_FLOAT_EQ(SampleConverter<float>::FromFloat(sample), sample);
    EXPECT_FLOAT_EQ(SampleConverter<float>::ToFloat(sample), sample);

    // Back and forth conversions.
    EXPECT_FLOAT_EQ(SampleConverter<float>::FromFloat(SampleConverter<float>::ToFloat(sample)),
                    sample);
    EXPECT_FLOAT_EQ(SampleConverter<float>::ToFloat(SampleConverter<float>::FromFloat(sample)),
                    sample);
  }

  // Should not be normalized.
  EXPECT_FLOAT_EQ(SampleConverter<float>::FromFloat(5.5f), 5.5f);
  EXPECT_FLOAT_EQ(SampleConverter<float>::FromFloat(-1.1f), -1.1f);

  EXPECT_FLOAT_EQ(SampleConverter<float>::ToFloat(5.5f), 5.5f);
  EXPECT_FLOAT_EQ(SampleConverter<float>::ToFloat(-1.1f), -1.1f);
}

}  // namespace
}  // namespace media_audio
