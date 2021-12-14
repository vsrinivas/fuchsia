// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "memory_patterns.h"

#include <vector>

#include <gtest/gtest.h>

#include "memory_range.h"

namespace hwstress {
namespace {

TEST(Util, RotatePattern) {
  ASSERT_EQ(RotatePattern({}, 5), std::vector<uint64_t>{});

  ASSERT_EQ(RotatePattern({0x11223344'aabbccdd}, 0), std::vector<uint64_t>{0x112233'44aabbccdd});
  ASSERT_EQ(RotatePattern({0x11223344'aabbccdd}, 8), std::vector<uint64_t>{0xdd112233'44aabbcc});
  ASSERT_EQ(RotatePattern({0x00000000'00000001}, 1), std::vector<uint64_t>{0x80000000'00000000});
  ASSERT_EQ(RotatePattern({0x80000000'00000000}, 63), std::vector<uint64_t>{0x00000000'00000001});

  ASSERT_EQ(RotatePattern({0xaaaaaaaa'aaaaaaaa, 0xbbbbbbbb'bbbbbbbb, 0xcccccccc'cccccccc}, 8),
            (std::vector<uint64_t>{0xccaaaaaa'aaaaaaaa, 0xaabbbbbb'bbbbbbbb, 0xbbcccccc'cccccccc}));
}

TEST(Util, NegateWords) {
  ASSERT_EQ(NegateWords({0xffff'ffff'ffff'ffff}), std::vector<uint64_t>{0x0000'0000'0000'0000});
  ASSERT_EQ(NegateWords({0x0000'0000'0000'0000}), std::vector<uint64_t>{0xffff'ffff'ffff'ffff});
}

TEST(WritePattern, Simple) {
  // Write out a simple pattern to memory.
  std::vector<uint8_t> memory;
  memory.resize(zx_system_get_page_size());
  WritePattern(memory, SimplePattern(0x55555555'55555555ul));

  // Ensure it was written correctly.
  for (uint8_t x : memory) {
    EXPECT_EQ(x, 0x55);
  }
}

TEST(SimplePattern, EndianCheck) {
  // Write out a pattern to memory.
  std::vector<uint8_t> memory;
  memory.resize(zx_system_get_page_size());
  WritePattern(memory, SimplePattern(0x00112233'44556677ul));

  // Ensure that bytes were written in the correct (big-endian) order.
  for (size_t i = 0; i < ZX_PAGE_SIZE; i += 8) {
    EXPECT_EQ(memory[i + 0], 0x00);
    EXPECT_EQ(memory[i + 1], 0x11);
    EXPECT_EQ(memory[i + 2], 0x22);
    EXPECT_EQ(memory[i + 3], 0x33);
    EXPECT_EQ(memory[i + 4], 0x44);
    EXPECT_EQ(memory[i + 5], 0x55);
    EXPECT_EQ(memory[i + 6], 0x66);
    EXPECT_EQ(memory[i + 7], 0x77);
  }
}

TEST(MultiWordPattern, EndianCheck) {
  // Write out a pattern to memory.
  std::vector<uint8_t> memory;
  memory.resize(zx_system_get_page_size());
  WritePattern(memory, MultiWordPattern({0x00112233'44556677ul, 0x8899aabb'ccddeeff}));

  // Ensure that bytes were written in the correct (big-endian) order.
  for (size_t i = 0; i < ZX_PAGE_SIZE; i += 16) {
    EXPECT_EQ(memory[i + 0], 0x00);
    EXPECT_EQ(memory[i + 1], 0x11);
    EXPECT_EQ(memory[i + 2], 0x22);
    EXPECT_EQ(memory[i + 3], 0x33);
    EXPECT_EQ(memory[i + 4], 0x44);
    EXPECT_EQ(memory[i + 5], 0x55);
    EXPECT_EQ(memory[i + 6], 0x66);
    EXPECT_EQ(memory[i + 7], 0x77);

    EXPECT_EQ(memory[i + 8], 0x88);
    EXPECT_EQ(memory[i + 9], 0x99);
    EXPECT_EQ(memory[i + 10], 0xaa);
    EXPECT_EQ(memory[i + 11], 0xbb);
    EXPECT_EQ(memory[i + 12], 0xcc);
    EXPECT_EQ(memory[i + 13], 0xdd);
    EXPECT_EQ(memory[i + 14], 0xee);
    EXPECT_EQ(memory[i + 15], 0xff);
  }
}

TEST(VerifyPattern, Simple) {
  // Write out a pattern to memory, and ensure it verifies correctly.
  std::vector<uint8_t> memory;
  memory.resize(zx_system_get_page_size());
  memset(memory.data(), 0x55, memory.size());
  EXPECT_EQ(std::nullopt, VerifyPattern(memory, SimplePattern(0x55555555'55555555)));

  // Change the memory to have incorrect bytes at various locations, and ensure
  // we see the errors.
  for (int bad_byte_index :
       std::initializer_list<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, ZX_PAGE_SIZE - 1}) {
    memset(memory.data(), 0x55, memory.size());
    memory[bad_byte_index] = 0x0;
    EXPECT_TRUE(VerifyPattern(memory, SimplePattern(0x55555555'55555555)).has_value());
  }
}

TEST(RandomPattern, EveryBitSet) {
  // Generate random patterns. Ensure that we see every bit as a "1" and every
  // bit as a "0" at least once. (An rng engine used during development was only
  // producing 63-bits of output, for example.)
  auto pattern = RandomPattern();
  uint64_t seen_one_bit = 0;
  uint64_t seen_zero_bit = 0;
  for (int i = 0; i < 1000; i++) {
    uint64_t x = pattern();
    seen_one_bit |= x;
    seen_zero_bit |= ~x;
  }
  EXPECT_EQ(seen_one_bit, ~0ul) << "After 1000 iterations, at least 1 bit hasn't been seen as 0";
  EXPECT_EQ(seen_zero_bit, ~0ul) << "After 1000 iterations, at least 1 bit hasn't been seen as 1";
}

}  // namespace
}  // namespace hwstress
