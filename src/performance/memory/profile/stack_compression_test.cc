// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/performance/memory/profile/stack_compression.h"

#include <array>
#include <random>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

namespace {
using testing::ElementsAre;
using testing::Eq;
using testing::Pointwise;

std::vector<uint8_t> compressed(std::vector<const uint64_t> input) {
  std::vector<uint8_t> compressed(input.size() * 9);
  cpp20::span result = compress(input, compressed);
  compressed.resize(result.size());
  return compressed;
}

inline constexpr unsigned char operator"" _u8(unsigned long long arg) noexcept {
  return static_cast<unsigned char>(arg);
}

TEST(StackCompressionTest, Varint) {
  // The first element is varint coded.
  EXPECT_THAT(compressed({0}), ElementsAre(0_u8));
  EXPECT_THAT(compressed({42}), ElementsAre(42_u8));
  EXPECT_THAT(compressed({0x7f}), ElementsAre(0x7f_u8));
  // It is expected that 8+ bits integer are 2 bytes.
  EXPECT_THAT(compressed({0x8f}), ElementsAre(0x8f_u8, 0x01_u8));
  EXPECT_THAT(compressed({0x8f77}), ElementsAre(0xf7_u8, 0x9e_u8, 0x02_u8));
}

TEST(StackCompressionTest, RollingXor) {
  // It is expected that the second value is xored by the first one.
  // Only the differeing bits are varint encoded.
  EXPECT_THAT(compressed({0xf00, 0xf05}), ElementsAre(0x80_u8, 0x1e_u8, 0x05_u8));
}

// TODO(https://fxbug.dev/111833): implement fuzz testing.
TEST(StackCompressionTest, BackAndForth) {
  for (uint64_t i = 0; i < 4086; i++) {
    // Random length in [0 ; 63].
    size_t len = random() & 63;

    // Random input array.
    uint64_t input[64];
    cpp20::span<uint64_t> input_span = {input, len};
    for (auto &v : input_span) {
      v = random();
    }

    // Maximum length of a single varint is 9 bytes.
    uint8_t compressed[std::size(input) * 9] = {0};
    uint64_t output[std::size(compressed)];
    auto output_span = decompress(compress({input, len}, compressed), output);
    EXPECT_THAT(output_span, Pointwise(Eq(), input_span));
  }
}

}  // namespace
