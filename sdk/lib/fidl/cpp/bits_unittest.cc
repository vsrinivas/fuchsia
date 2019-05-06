// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include "gtest/gtest.h"
#include "lib/fidl/cpp/builder.h"

namespace fidl {
namespace test {
namespace misc {
namespace {

TEST(Bits, BitwiseOperators) {
    auto b_or_d = SampleBits::B | SampleBits::D;
    EXPECT_EQ(static_cast<uint32_t>(b_or_d), 6u /* 2 | 4*/);

    auto b_or_e = SampleBits::B | SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(b_or_e), 10u /* 2 | 8*/);

    auto not_b = ~SampleBits::B;
    EXPECT_EQ(static_cast<uint32_t>(not_b), 12u /* ~2 & (2 | 4 | 8)*/);

    auto not_d = ~SampleBits::D;
    EXPECT_EQ(static_cast<uint32_t>(not_d), 10u /* ~4 & (2 | 4 | 8)*/);

    auto not_e = ~SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(not_e), 6u /* ~8 & (2 | 4 | 8)*/);

    auto b_and_not_e = SampleBits::B & ~SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(b_and_not_e), 2u /* 2 & 6*/);

    auto b_or_d_and_b_or_e = (SampleBits::B | SampleBits::D) & (SampleBits::B | SampleBits::E);
    EXPECT_EQ(static_cast<uint32_t>(b_or_d_and_b_or_e), 2u /* 6 & 10*/);

    auto b_xor_not_e = SampleBits::B ^ ~SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

    auto b_or_d_xor_b_or_e = (SampleBits::B | SampleBits::D) ^ (SampleBits::B | SampleBits::E);
    EXPECT_EQ(static_cast<uint32_t>(b_or_d_xor_b_or_e), 12u /* 6 ^ 10*/);
}

TEST(Bits, BitwiseAssignOperators) {
    auto b_or_d = SampleBits::B;
    b_or_d |= SampleBits::D;
    EXPECT_EQ(static_cast<uint32_t>(b_or_d), 6u /* 2 | 4*/);

    auto b_and_not_e = SampleBits::B;
    b_and_not_e &= ~SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(b_and_not_e), 2u /* 2 & 6*/);

    auto b_xor_not_e = SampleBits::B;
    b_xor_not_e ^= ~SampleBits::E;
    EXPECT_EQ(static_cast<uint32_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

    EXPECT_EQ(static_cast<uint32_t>(SampleBits::B), 2u);
    EXPECT_EQ(static_cast<uint32_t>(SampleBits::D), 4u);
    EXPECT_EQ(static_cast<uint32_t>(SampleBits::E), 8u);
}

TEST(Bits, IsConstexpr) {
    static constexpr auto this_should_compile = SampleBits::B | SampleBits::D | SampleBits::E;
    EXPECT_EQ(this_should_compile, SampleBitsMask);
}

}  // namespace
}  // namespace misc
}  // namespace test
}  // namespace fidl
