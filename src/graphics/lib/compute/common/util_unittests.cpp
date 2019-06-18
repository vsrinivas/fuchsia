// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "util.h"

namespace {

TEST(common, is_pow2_u32)
{
  for (uint32_t power = 0; power < 32; ++power)
    {
      uint32_t value = 1U << power;
      EXPECT_TRUE(is_pow2_u32(value)) << power;
      if (power > 1)
        EXPECT_FALSE(is_pow2_u32(value + 1)) << power;
      if (power > 2)
        EXPECT_FALSE(is_pow2_u32(value - 1)) << power;
    }
}

TEST(common, pow2_ru_u32)
{
  // Sanity check for all powers of 2.
  for (uint32_t power = 0; power < 32; ++power)
    {
      uint32_t value = 1U << power;
      EXPECT_EQ(value, pow2_ru_u32(value)) << power;
    }
  // Check a few other random values.
  static const struct
  {
    uint32_t input;
    uint32_t expected;
  } kData[] = {
    { 0, 0 },
    { 3, 4 },
    { 5, 8 },
    { 6, 8 },
    { 7, 8 },
    { 0x3fff, 0x4000 },
    { 0x7fffffffU, 0x80000000U },
    { 0x80000000U, 0x80000000U },
  };
  for (const auto & data : kData)
    {
      EXPECT_EQ(data.expected, pow2_ru_u32(data.input)) << data.input;
    }
}

TEST(common, pow2_rd_u32)
{
  // Sanity check for all powers of 2.
  for (uint32_t power = 0; power < 32; ++power)
    {
      uint32_t value = 1U << power;
      EXPECT_EQ(value, pow2_rd_u32(value)) << power;
    }
  // Check a few other random values.
  static const struct
  {
    uint32_t input;
    uint32_t expected;
  } kData[] = {
    { 3, 2 },
    { 5, 4 },
    { 6, 4 },
    { 7, 4 },
    { 9, 8 },
    { 18, 16 },
    { 0x3fff, 0x2000 },
    { 0x7fffffffU, 0x40000000U },
    { 0x80000001U, 0x80000000U },
  };
  for (const auto & data : kData)
    {
      EXPECT_EQ(data.expected, pow2_rd_u32(data.input)) << data.input;
    }
}

TEST(common, msb_idx_u32)
{
  // Sanity check for all powers of 2.
  for (uint32_t power = 0; power < 32; ++power)
    {
      uint32_t value = 1U << power;
      EXPECT_EQ(power, msb_idx_u32(value)) << power;
    }
  // Check a few other random values.
  static const struct
  {
    uint32_t input;
    uint32_t expected;
  } kData[] = {
    { 3, 1 },
    { 5, 2 },
    { 6, 2 },
    { 7, 2 },
    { 9, 3 },
    { 17, 4 },
    { 0x34567, 17 },
    { 0x7fffffffU, 30 },
    { 0x80000000U, 31 },
    { 0x80000001U, 31 },
  };
  for (const auto & data : kData)
    {
      EXPECT_EQ(data.expected, msb_idx_u32(data.input)) << data.input;
    }
}

}  // namespace
