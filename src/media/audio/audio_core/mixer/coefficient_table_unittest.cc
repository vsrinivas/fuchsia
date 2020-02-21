// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/media/audio/audio_core/mixer/coefficient_table.h"

#include <gtest/gtest.h>

#include "src/media/audio/audio_core/mixer/frames.h"

namespace media::audio::mixer {
namespace {

TEST(CoefficientTableTest, AllIndicesAccessible) {
  FractionalFrames<uint32_t> width(10);
  CoefficientTable table(width.raw_value(), FractionalFrames<uint32_t>::Format::FractionalBits);
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    table[i] = static_cast<float>(i);
  }
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    ASSERT_FLOAT_EQ(table[i], static_cast<float>(i));
  }
}

TEST(CoefficientTableTest, IntegralStrideHasPhysicallyContiguousIndicies) {
  FractionalFrames<uint32_t> width(10);
  CoefficientTable table(width.raw_value(), FractionalFrames<uint32_t>::Format::FractionalBits);

  const auto FRAC_BITS = FractionalFrames<uint32_t>::Format::FractionalBits;
  const auto FRAC_ONE = 1 << FRAC_BITS;
  for (uint32_t fraction = 0; fraction < FRAC_ONE; ++fraction) {
    // Each fractional value will have a block in the vector. Now check that every valid integral
    // value is contiguous for this fractional value.
    uint32_t block_index = fraction * width.Ceiling();
    for (uint32_t integer = 0; integer < width.Ceiling(); ++integer) {
      auto fixed_value = (integer << FRAC_BITS) + fraction;
      ASSERT_EQ(block_index + integer, table.PhysicalIndex(fixed_value));
    }
  }
}

TEST(CoefficientTableTest, AcceptFractionalWidth) {
  const auto FRAC_BITS = FractionalFrames<uint32_t>::Format::FractionalBits;
  auto width = FractionalFrames<uint32_t>::FromRaw((5 << FRAC_BITS) + ((1 << FRAC_BITS) / 2));
  CoefficientTable table(width.raw_value(), FractionalFrames<uint32_t>::Format::FractionalBits);
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    table[i] = static_cast<float>(i);
  }
  for (uint32_t i = 0; i < width.raw_value(); ++i) {
    ASSERT_FLOAT_EQ(table[i], static_cast<float>(i));
  }
}

}  // namespace
}  // namespace media::audio::mixer
