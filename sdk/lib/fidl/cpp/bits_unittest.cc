// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/builder.h"

// The following APIs tested are shared between strict and flexible bits, but
// are tested separately.

// TODO(fxb/62890): Update these to use TYPED_TEST equivalents when they become
// implemented in zxtest.

TEST(StrictBits, StrictBitwiseOperators) {
  using fidl::test::misc::StrictBits;
  StrictBits b_or_d = StrictBits::B | StrictBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  StrictBits b_or_e = StrictBits::B | StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_or_e), 10u /* 2 | 8*/);

  StrictBits not_b = ~StrictBits::B;
  EXPECT_EQ(static_cast<uint8_t>(not_b), 12u /* ~2 & (2 | 4 | 8)*/);

  StrictBits not_d = ~StrictBits::D;
  EXPECT_EQ(static_cast<uint8_t>(not_d), 10u /* ~4 & (2 | 4 | 8)*/);

  StrictBits not_e = ~StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(not_e), 6u /* ~8 & (2 | 4 | 8)*/);

  StrictBits b_and_not_e = StrictBits::B & ~StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  StrictBits b_or_d_and_b_or_e = (StrictBits::B | StrictBits::D) & (StrictBits::B | StrictBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_and_b_or_e), 2u /* 6 & 10*/);

  StrictBits b_xor_not_e = StrictBits::B ^ ~StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  StrictBits b_or_d_xor_b_or_e = (StrictBits::B | StrictBits::D) ^ (StrictBits::B | StrictBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_xor_b_or_e), 12u /* 6 ^ 10*/);
}

TEST(StrictBits, StrictBitwiseAssignOperators) {
  using fidl::test::misc::StrictBits;
  StrictBits b_or_d = StrictBits::B;
  b_or_d |= StrictBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  StrictBits b_and_not_e = StrictBits::B;
  b_and_not_e &= ~StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  StrictBits b_xor_not_e = StrictBits::B;
  b_xor_not_e ^= ~StrictBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  EXPECT_EQ(static_cast<uint8_t>(StrictBits::B), 2u);
  EXPECT_EQ(static_cast<uint8_t>(StrictBits::D), 4u);
  EXPECT_EQ(static_cast<uint8_t>(StrictBits::E), 8u);
}

TEST(StrictBits, CanConvertStrictBitsToNumberButMustBeExplicit) {
  using fidl::test::misc::StrictBits;
  uint8_t r8 = static_cast<uint8_t>(StrictBits::B);
  EXPECT_EQ(r8, 2u);
  uint16_t r16 = static_cast<uint8_t>(StrictBits::B);
  EXPECT_EQ(r16, 2u);
}

TEST(StrictBits, CanConvertStrictBitsToBool) {
  using fidl::test::misc::StrictBits;
  bool result = static_cast<bool>(StrictBits::B);
  EXPECT_TRUE(result);
}

TEST(FlexibleBits, FlexibleBitwiseOperators) {
  using fidl::test::misc::FlexibleBits;
  FlexibleBits b_or_d = FlexibleBits::B | FlexibleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  FlexibleBits b_or_e = FlexibleBits::B | FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_or_e), 10u /* 2 | 8*/);

  FlexibleBits not_b = ~FlexibleBits::B;
  EXPECT_EQ(static_cast<uint8_t>(not_b), 12u /* ~2 & (2 | 4 | 8)*/);

  FlexibleBits not_d = ~FlexibleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(not_d), 10u /* ~4 & (2 | 4 | 8)*/);

  FlexibleBits not_e = ~FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(not_e), 6u /* ~8 & (2 | 4 | 8)*/);

  FlexibleBits b_and_not_e = FlexibleBits::B & ~FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  FlexibleBits b_or_d_and_b_or_e = (FlexibleBits::B | FlexibleBits::D) & (FlexibleBits::B | FlexibleBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_and_b_or_e), 2u /* 6 & 10*/);

  FlexibleBits b_xor_not_e = FlexibleBits::B ^ ~FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  FlexibleBits b_or_d_xor_b_or_e = (FlexibleBits::B | FlexibleBits::D) ^ (FlexibleBits::B | FlexibleBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_xor_b_or_e), 12u /* 6 ^ 10*/);
}

TEST(FlexibleBits, FlexibleBitwiseAssignOperators) {
  using fidl::test::misc::FlexibleBits;
  FlexibleBits b_or_d = FlexibleBits::B;
  b_or_d |= FlexibleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  FlexibleBits b_and_not_e = FlexibleBits::B;
  b_and_not_e &= ~FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  FlexibleBits b_xor_not_e = FlexibleBits::B;
  b_xor_not_e ^= ~FlexibleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  EXPECT_EQ(static_cast<uint8_t>(FlexibleBits::B), 2u);
  EXPECT_EQ(static_cast<uint8_t>(FlexibleBits::D), 4u);
  EXPECT_EQ(static_cast<uint8_t>(FlexibleBits::E), 8u);
}

TEST(FlexibleBits, CanConvertFlexibleBitsToNumberButMustBeExplicit) {
  using fidl::test::misc::FlexibleBits;
  uint8_t r8 = static_cast<uint8_t>(FlexibleBits::B);
  EXPECT_EQ(r8, 2u);
  uint16_t r16 = static_cast<uint8_t>(FlexibleBits::B);
  EXPECT_EQ(r16, 2u);
}

TEST(FlexibleBits, CanConvertFlexibleBitsToBool) {
  using fidl::test::misc::FlexibleBits;
  bool result = static_cast<bool>(FlexibleBits::B);
  EXPECT_TRUE(result);
}


// The following APIs tested are only available on strict bits;

TEST(StrictBits, IsConstexprAndMask) {
  using fidl::test::misc::StrictBits;
  static constexpr auto this_should_compile = StrictBits::B | StrictBits::D | StrictBits::E;
  EXPECT_EQ(this_should_compile, fidl::test::misc::StrictBitsMask);
}

// The following APIs tested are only available on flexible bits.

TEST(FlexibleBits, TruncatingUnknown) {
  using BitsType = fidl::test::misc::FlexibleBits;
  // The bits type only has 2, 4, and 8 defined.
  auto bits = BitsType::TruncatingUnknown(1);
  EXPECT_EQ(static_cast<uint8_t>(bits), 0);
}

TEST(FlexibleBits, TryFrom) {
  using BitsType = fidl::test::misc::FlexibleBits;
  // The bits type only has 2, 4, and 8 defined.
  auto result = BitsType::TryFrom(1);
  EXPECT_EQ(result, fit::nullopt);

  auto result_ok = BitsType::TryFrom(2);
  EXPECT_EQ(result_ok, fit::optional<BitsType>(BitsType::B));
}

TEST(FlexibleBits, QueryingUnknown) {
  using BitsType = fidl::test::misc::FlexibleBits;
  // The bits type only has 2, 4, and 8 defined.
  auto bits = static_cast<BitsType>(2 | 1);
  EXPECT_TRUE(bits.has_unknown_bits());
  EXPECT_EQ(static_cast<uint8_t>(bits.unknown_bits()), 1);

  bits = BitsType::TruncatingUnknown(2 | 1);
  EXPECT_FALSE(bits.has_unknown_bits());
  EXPECT_EQ(static_cast<uint8_t>(bits.unknown_bits()), 0);
}

TEST(FlexibleBits, IsConstexprAndMask) {
  using fidl::test::misc::FlexibleBits;
  static constexpr auto this_should_compile = FlexibleBits::B | FlexibleBits::D | FlexibleBits::E;
  EXPECT_EQ(this_should_compile, fidl::test::misc::FlexibleBits::kMask);
}
