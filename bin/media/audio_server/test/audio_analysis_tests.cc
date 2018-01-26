// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include "audio_analysis.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Test uint8 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers8) {
  std::array<uint8_t, 2> source = {0x42, 0x55};
  std::array<uint8_t, 2> expect = {0x42, 0xAA};

  // First values match ...
  EXPECT_TRUE(CompareBuffers<uint8_t>(source.data(), expect.data(), 1));
  // ... but entire buffer does NOT
  EXPECT_FALSE(CompareBuffers<uint8_t>(source.data(), expect.data(), 2));
}

// Test int16 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers16) {
  std::array<int16_t, 3> source = {-1, 0x1157, 0x5555};
  std::array<int16_t, 3> expect = {-1, 0x1357, 0x5555};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers<int16_t>(source.data(), expect.data(), 3));
  // ... but the first values DO
  EXPECT_TRUE(CompareBuffers<int16_t>(source.data(), expect.data(), 1));
}

// Test int32 version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers32) {
  std::array<int32_t, 4> source = {0x13579BDF, 0x26AE048C, -0x76543210,
                                   0x1234567};
  std::array<int32_t, 4> expect = {0x13579BDF, 0x26AE048C, -0x76543210,
                                   0x7654321};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers<int32_t>(source.data(), expect.data(), 4));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers<int32_t>(source.data(), expect.data(), 3));
}

// Test uint8 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal8) {
  std::array<uint8_t, 2> source = {0xBB, 0xBB};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal<uint8_t>(source.data(), 0xBC, 2));
  // Match
  EXPECT_TRUE(CompareBufferToVal<uint8_t>(source.data(), 0xBB, 2));
}

// Test int16 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal16) {
  std::array<int16_t, 2> source = {0xBAD, 0xCAD};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal<int16_t>(source.data(), 0xBAD, 2));
  // Match - if we only look at the second value
  EXPECT_TRUE(CompareBufferToVal<int16_t>(source.data() + 1, 0xCAD, 1));
}

// Test int32 version of this func, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffToVal32) {
  std::array<int32_t, 2> source = {0xF00CAFE, 0xBADF00D};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal<int32_t>(source.data(), 0xF00CAFE, 2));
  // Match - if we only look at the first value
  EXPECT_TRUE(CompareBufferToVal<int32_t>(source.data(), 0xF00CAFE, 1));
}

}  // namespace test
}  // namespace media