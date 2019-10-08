// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/filter.h"

#include <gtest/gtest.h>

#include "src/media/audio/lib/logging/logging.h"

namespace media::audio::mixer {
namespace {

TEST(PointFilterTest, Construction) {
  PointFilter filter;
  auto source_rate = 48000u;                // default
  auto dest_rate = 48000u;                  // default
  auto num_frac_bits = kPtsFractionalBits;  // default
  auto expected_side_width = (1u << (num_frac_bits - 1u)) + 1u;

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), expected_side_width);
  EXPECT_EQ(filter.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 1.0);

  source_rate = 16000u;
  dest_rate = 48000u;
  PointFilter filter2(source_rate, dest_rate);
  num_frac_bits = kPtsFractionalBits;  // default
  expected_side_width = (1u << (num_frac_bits - 1u)) + 1u;

  EXPECT_EQ(filter2.source_rate(), source_rate);
  EXPECT_EQ(filter2.dest_rate(), dest_rate);
  EXPECT_EQ(filter2.side_width(), expected_side_width);
  EXPECT_EQ(filter2.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter2.rate_conversion_ratio(), 3.0);

  source_rate = 44100u;
  dest_rate = 22050u;
  num_frac_bits = 4;
  PointFilter filter3(source_rate, dest_rate, num_frac_bits);
  expected_side_width = (1u << (num_frac_bits - 1u)) + 1u;

  EXPECT_EQ(filter3.source_rate(), source_rate);
  EXPECT_EQ(filter3.dest_rate(), dest_rate);
  EXPECT_EQ(filter3.side_width(), expected_side_width);
  EXPECT_EQ(filter3.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter3.rate_conversion_ratio(), 0.5);
}

TEST(LinearFilterTest, Construction) {
  LinearFilter filter;
  auto source_rate = 48000u;                // default
  auto dest_rate = 48000u;                  // default
  auto num_frac_bits = kPtsFractionalBits;  // default
  auto expected_side_width = 1u << num_frac_bits;

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), expected_side_width);
  EXPECT_EQ(filter.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 1.0);

  source_rate = 32000u;
  dest_rate = 96000u;
  LinearFilter filter2(source_rate, dest_rate);
  num_frac_bits = kPtsFractionalBits;  // default
  expected_side_width = 1u << num_frac_bits;

  EXPECT_EQ(filter2.source_rate(), source_rate);
  EXPECT_EQ(filter2.dest_rate(), dest_rate);
  EXPECT_EQ(filter2.side_width(), expected_side_width);
  EXPECT_EQ(filter2.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter2.rate_conversion_ratio(), 3.0);

  source_rate = 96000u;
  dest_rate = 48000u;
  num_frac_bits = 6;
  LinearFilter filter3(source_rate, dest_rate, num_frac_bits);
  expected_side_width = 1u << num_frac_bits;

  EXPECT_EQ(filter3.source_rate(), source_rate);
  EXPECT_EQ(filter3.dest_rate(), dest_rate);
  EXPECT_EQ(filter3.side_width(), expected_side_width);
  EXPECT_EQ(filter3.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter3.rate_conversion_ratio(), 0.5);
}

TEST(SincFilterTest, Construction) {
  SincFilter filter;
  auto source_rate = 48000u;                           // default
  auto dest_rate = 48000u;                             // default
  auto num_taps = kSincFilterSideTaps;                 // default
  auto num_frac_bits = kPtsFractionalBits;             // default
  auto side_width = (num_taps + 1u) << num_frac_bits;  // default

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), side_width);
  EXPECT_EQ(filter.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 1.0);

  source_rate = 32000u;
  dest_rate = 96000u;
  SincFilter filter2(source_rate, dest_rate);
  num_taps = kSincFilterSideTaps;                 // default
  num_frac_bits = kPtsFractionalBits;             // default
  side_width = (num_taps + 1u) << num_frac_bits;  // default

  EXPECT_EQ(filter2.source_rate(), source_rate);
  EXPECT_EQ(filter2.dest_rate(), dest_rate);
  EXPECT_EQ(filter2.side_width(), side_width);
  EXPECT_EQ(filter2.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter2.rate_conversion_ratio(), 3.0);

  source_rate = 96000u;
  dest_rate = 48000u;
  num_taps = 9u;
  num_frac_bits = kPtsFractionalBits;  // default
  side_width = (num_taps + 1u) << num_frac_bits;
  SincFilter filter3(source_rate, dest_rate, side_width);

  EXPECT_EQ(filter3.source_rate(), source_rate);
  EXPECT_EQ(filter3.dest_rate(), dest_rate);
  EXPECT_EQ(filter3.side_width(), side_width);
  EXPECT_EQ(filter3.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter3.rate_conversion_ratio(), 0.5);

  source_rate = 16000u;
  dest_rate = 96000u;
  num_taps = 10u;
  num_frac_bits = 4u;
  side_width = (num_taps + 1u) << num_frac_bits;
  SincFilter filter4(source_rate, dest_rate, side_width, num_frac_bits);

  EXPECT_EQ(filter4.source_rate(), source_rate);
  EXPECT_EQ(filter4.dest_rate(), dest_rate);
  EXPECT_EQ(filter4.side_width(), side_width);
  EXPECT_EQ(filter4.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter4.rate_conversion_ratio(), 6.0);
}

TEST(PointFilterTest, FilterCoefficients) {
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_frac_bits = 4u;

  PointFilter filter(source_rate, dest_rate, num_frac_bits);

  auto expected_side_width = (1u << (num_frac_bits - 1u)) + 1u;
  EXPECT_EQ(filter.side_width(), expected_side_width);

  EXPECT_FLOAT_EQ(filter[0], 1.0f);
  auto frac_half = 1u << (num_frac_bits - 1u);
  for (auto idx = 1u; idx < frac_half; ++idx) {
    EXPECT_FLOAT_EQ(filter[idx], 1.0f);
  }

  EXPECT_FLOAT_EQ(filter[frac_half], 0.5f);
  for (auto idx = frac_half + 1u; idx < expected_side_width; ++idx) {
    EXPECT_FLOAT_EQ(filter[idx], 0.0f);
  }
}

TEST(LinearFilterTest, FilterCoefficients) {
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_frac_bits = 6u;

  LinearFilter filter(source_rate, dest_rate, num_frac_bits);

  auto expected_side_width = 1u << num_frac_bits;
  EXPECT_EQ(filter.side_width(), expected_side_width);

  auto frac_size = 1u << num_frac_bits;
  for (auto idx = 0u; idx < frac_size; ++idx) {
    EXPECT_FLOAT_EQ(filter[idx], static_cast<float>(frac_size - idx) / frac_size);
  }
  for (auto idx = frac_size; idx < expected_side_width; ++idx) {
    EXPECT_FLOAT_EQ(filter[idx], 0.0f);
  }
}

TEST(SincFilterTest, FilterCoefficients_Unity) {
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_taps = 10u;
  auto num_frac_bits = 4u;
  auto side_width = (num_taps + 1u) << num_frac_bits;

  SincFilter filter(source_rate, dest_rate, side_width, num_frac_bits);

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), side_width);
  EXPECT_EQ(filter.num_frac_bits(), num_frac_bits);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 1.0f);

  EXPECT_FLOAT_EQ(filter[0], 1.0f);
  auto frac_size = 1u << num_frac_bits;
  auto frac_half = frac_size >> 1u;
  auto idx = frac_size;
  for (auto tap = 1u; tap <= num_taps; ++tap, idx += frac_size) {
    EXPECT_FLOAT_EQ(filter[idx], 0.0f) << "idx: " << idx;

    if (tap & 1u) {
      EXPECT_GT(filter[idx - frac_half], 0.0f);
      EXPECT_LT(filter[idx + frac_half], 0.0f);
    } else {
      EXPECT_LT(filter[idx - frac_half], 0.0f);
      EXPECT_GT(filter[idx + frac_half], 0.0f);
    }
  }
}

TEST(SincFilterTest, FilterCoefficients_DownSample) {
  auto source_rate = 48000u;
  auto dest_rate = 24000u;
  auto num_taps = 9u;
  auto num_frac_bits = 4u;
  auto side_width = (num_taps + 1u) << num_frac_bits;

  SincFilter filter(source_rate, dest_rate, side_width, num_frac_bits);

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), side_width);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 0.5f);
}

TEST(SincFilterTest, FilterCoefficients_UpSample) {
  auto source_rate = 24000u;
  auto dest_rate = 48000u;
  auto num_taps = 8u;
  auto num_frac_bits = 3u;
  auto side_width = (num_taps + 1u) << num_frac_bits;

  SincFilter filter(source_rate, dest_rate, side_width, num_frac_bits);

  EXPECT_EQ(filter.source_rate(), source_rate);
  EXPECT_EQ(filter.dest_rate(), dest_rate);
  EXPECT_EQ(filter.side_width(), side_width);
  EXPECT_FLOAT_EQ(filter.rate_conversion_ratio(), 2.0f);
}

// TODO(mpuryear): validate other rate-conversion ratios
TEST(PointFilterTest, ComputeSample) {
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_frac_bits = 4u;

  PointFilter filter(source_rate, dest_rate, num_frac_bits);

  auto frac_size = 1u << num_frac_bits;
  auto frac_half = frac_size >> 1u;
  float data[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  EXPECT_FLOAT_EQ(filter.ComputeSample(0, &data[1]), data[1]);
  EXPECT_FLOAT_EQ(filter.ComputeSample(frac_half, &data[2]), (data[2] + data[3]) / 2);
  EXPECT_FLOAT_EQ(filter.ComputeSample(frac_size - 1, &data[3]), data[4]);
}

// TODO(mpuryear): validate other rate-conversion ratios
TEST(LinearFilterTest, ComputeSample) {
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_frac_bits = 4u;

  LinearFilter filter(source_rate, dest_rate, num_frac_bits);

  auto frac_size = 1u << num_frac_bits;
  auto frac_half = frac_size >> 1u;
  auto frac_quarter = frac_size >> 2u;
  float data[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

  EXPECT_FLOAT_EQ(filter.ComputeSample(0, &data[1]), data[1]);
  EXPECT_FLOAT_EQ(filter.ComputeSample(frac_half, &data[2]), (data[2] + data[3]) / 2.0f);
  EXPECT_FLOAT_EQ(filter.ComputeSample(frac_half + frac_quarter, &data[3]),
                  (data[3] + 3.0f * data[4]) / 4.0f);
}

void ValidateSincComputeSample(uint32_t source_rate, uint32_t dest_rate, uint32_t side_width,
                               uint32_t num_frac_bits) {
  SincFilter filter(source_rate, dest_rate, side_width, num_frac_bits);

  float data[] = {0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f, 9.0f, 10.0f};
  auto frac_size = 1u << num_frac_bits;
  auto frac_half = frac_size >> 1u;
  auto frac_quarter = frac_half >> 1u;

  // These values should be precisely equal
  EXPECT_FLOAT_EQ(filter.ComputeSample(0, &data[3]), data[3]);
  EXPECT_FLOAT_EQ(filter.ComputeSample(0, &data[4]), data[4]);

  // These values are only calculated to a specific quality tolerance (related to side_width and
  // num_frac_bits), so the only SAFE things to do here are rough comparisons.
  EXPECT_GT(filter.ComputeSample(frac_half, &data[5]), data[5]);
  EXPECT_LT(filter.ComputeSample(frac_quarter, &data[4]),
            filter.ComputeSample(frac_half, &data[4]));
}

TEST(SincFilterTest, ComputeSample) {
  // Unity rate ratio
  auto source_rate = 48000u;
  auto dest_rate = 48000u;
  auto num_taps = 2u;
  auto num_frac_bits = 2u;
  auto side_width = (num_taps + 1u) << num_frac_bits;
  ValidateSincComputeSample(source_rate, dest_rate, side_width, num_frac_bits);

  // Up-sampling rate ratio
  source_rate = 24000u;
  dest_rate = 48000u;
  num_taps = 3u;
  num_frac_bits = 2u;
  side_width = (num_taps + 1u) << num_frac_bits;
  ValidateSincComputeSample(source_rate, dest_rate, side_width, num_frac_bits);

  // Down-sampling rate ratio
  source_rate = 48000u;
  dest_rate = 24000u;
  num_taps = 3u;
  num_frac_bits = 3u;
  side_width = (num_taps + 1u) << num_frac_bits;
  ValidateSincComputeSample(source_rate, dest_rate, side_width, num_frac_bits);
}

}  // namespace
}  // namespace media::audio::mixer
