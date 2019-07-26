// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <array>
#include <lib/affine/utils.h>
#include <limits>
#include <zxtest/zxtest.h>

namespace {

constexpr int64_t MAX = std::numeric_limits<int64_t>::max();
constexpr int64_t MIN = std::numeric_limits<int64_t>::min();

struct TestVector {
  int64_t a, b, expected;
};

TEST(UtilsTestCase, ClampAdd) {
  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{       15,       25,      40 },
        TestVector{       15,      -25,     -10 },

        TestVector{       15, MAX - 16, MAX - 1 },
        TestVector{       15, MAX - 15, MAX - 0 },
        TestVector{       15, MAX - 14, MAX - 0 },

        TestVector{ MAX - 16,       15, MAX - 1 },
        TestVector{ MAX - 15,       15, MAX - 0 },
        TestVector{ MAX - 14,       15, MAX - 0 },

        TestVector{      -15, MIN + 16, MIN + 1 },
        TestVector{      -15, MIN + 15, MIN + 0 },
        TestVector{      -15, MIN + 14, MIN + 0 },

        TestVector{ MIN + 16,      -15, MIN + 1 },
        TestVector{ MIN + 15,      -15, MIN + 0 },
        TestVector{ MIN + 14,      -15, MIN + 0 },

        TestVector{      MAX,  MAX - 1,     MAX },
        TestVector{  MAX - 1,      MAX,     MAX },
        TestVector{      MAX,      MAX,     MAX },
    };
  // clang-format on

  for (const auto& v : TEST_VECTORS) {
    int64_t result = affine::utils::ClampAdd(v.a, v.b);
    EXPECT_EQ(v.expected, result, "test case: 0x%lx + 0x%lx", v.a, v.b);
  }
}

TEST(UtilsTestCase, ClampSub) {
  // clang-format off
    constexpr std::array TEST_VECTORS {
        TestVector{       15,       25,     -10 },
        TestVector{       15,      -25,      40 },

        TestVector{      -15, MAX - 16, MIN + 2 },
        TestVector{      -15, MAX - 15, MIN + 1 },
        TestVector{      -15, MAX - 14, MIN + 0 },
        TestVector{      -15, MAX - 13, MIN + 0 },

        TestVector{ MIN + 16,       15, MIN + 1 },
        TestVector{ MIN + 15,       15, MIN + 0 },
        TestVector{ MIN + 14,       15, MIN + 0 },

        TestVector{       15, MIN + 15, MAX - 0 },
        TestVector{       15, MIN + 16, MAX - 0 },
        TestVector{       15, MIN + 17, MAX - 1 },

        TestVector{ MAX - 16,      -15, MAX - 1 },
        TestVector{ MAX - 15,      -15, MAX - 0 },
        TestVector{ MAX - 14,      -15, MAX - 0 },

        TestVector{        0,  MIN + 0, MAX - 0 },
        TestVector{        0,  MIN + 1, MAX - 0 },
        TestVector{        0,  MIN + 2, MAX - 1 },

        TestVector{      MIN,  MIN + 1,      -1 },
        TestVector{      MIN,      MIN,       0 },

    };
  // clang-format on

  for (const auto& v : TEST_VECTORS) {
    int64_t result = affine::utils::ClampSub(v.a, v.b);
    EXPECT_EQ(v.expected, result, "test case: 0x%lx - 0x%lx", v.a, v.b);
  }
}

}  // namespace
