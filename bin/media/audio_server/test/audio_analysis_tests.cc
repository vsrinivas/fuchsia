// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fbl/algorithm.h>

#include "audio_analysis.h"
#include "gtest/gtest.h"

namespace media {
namespace test {

// Test uint8 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers8) {
  uint8_t source[] = {0x42, 0x55};
  uint8_t expect[] = {0x42, 0xAA};

  // First values match ...
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
  // ... but entire buffer does NOT
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
}

// Test int16 version of CompareBuffers, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffers16) {
  int16_t source[] = {-1, 0x1157, 0x5555};
  int16_t expect[] = {-1, 0x1357, 0x5555};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first values DO
  EXPECT_TRUE(CompareBuffers(source, expect, 1));
}

// Test int32 version of CompareBuffers, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffers32) {
  int32_t source[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x1234567};
  int32_t expect[] = {0x13579BDF, 0x26AE048C, -0x76543210, 0x7654321};

  // Buffers do not match ...
  EXPECT_FALSE(CompareBuffers(source, expect, fbl::count_of(source), false));
  // ... but the first three values DO
  EXPECT_TRUE(CompareBuffers(source, expect, fbl::count_of(source) - 1));
}

// Test uint8 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal8) {
  uint8_t source[] = {0xBB, 0xBB};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<uint8_t>(0xBC),
                                  fbl::count_of(source), false));
  // Match
  EXPECT_TRUE(
      CompareBufferToVal(source, static_cast<uint8_t>(0xBB), fbl::count_of(source)));
}

// Test int16 version of this func, which we use to test output buffers
TEST(AnalysisHelpers, CompareBuffToVal16) {
  int16_t source[] = {0xBAD, 0xCAD};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, static_cast<int16_t>(0xBAD),
                                  fbl::count_of(source), false));
  // Match - if we only look at the second value
  EXPECT_TRUE(CompareBufferToVal(source + 1, static_cast<int16_t>(0xCAD), 1));
}

// Test int32 version of this func, which we use to test accum buffers
TEST(AnalysisHelpers, CompareBuffToVal32) {
  int32_t source[] = {0xF00CAFE, 0xBADF00D};

  // No match ...
  EXPECT_FALSE(CompareBufferToVal(source, 0xF00CAFE, fbl::count_of(source), false));
  // Match - if we only look at the first value
  EXPECT_TRUE(CompareBufferToVal(source, 0xF00CAFE, 1));
}

}  // namespace test
}  // namespace media
