// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include "gtest/gtest.h"

TEST(Bits, BitwiseOperators) {
  using namespace llcpp::fidl::llcpp::types::test;

  auto b_or_d = SampleBits::B | SampleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  auto b_or_e = SampleBits::B | SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_or_e), 10u /* 2 | 8*/);

  auto not_b = ~SampleBits::B;
  EXPECT_EQ(static_cast<uint8_t>(not_b), 12u /* ~2 & (2 | 4 | 8)*/);

  auto not_d = ~SampleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(not_d), 10u /* ~4 & (2 | 4 | 8)*/);

  auto not_e = ~SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(not_e), 6u /* ~8 & (2 | 4 | 8)*/);

  auto b_and_not_e = SampleBits::B & ~SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  auto b_or_d_and_b_or_e = (SampleBits::B | SampleBits::D) & (SampleBits::B | SampleBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_and_b_or_e), 2u /* 6 & 10*/);

  auto b_xor_not_e = SampleBits::B ^ ~SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  auto b_or_d_xor_b_or_e = (SampleBits::B | SampleBits::D) ^ (SampleBits::B | SampleBits::E);
  EXPECT_EQ(static_cast<uint8_t>(b_or_d_xor_b_or_e), 12u /* 6 ^ 10*/);
}

TEST(Bits, BitwiseAssignOperators) {
  using namespace llcpp::fidl::llcpp::types::test;

  auto b_or_d = SampleBits::B;
  b_or_d |= SampleBits::D;
  EXPECT_EQ(static_cast<uint8_t>(b_or_d), 6u /* 2 | 4*/);

  auto b_and_not_e = SampleBits::B;
  b_and_not_e &= ~SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_and_not_e), 2u /* 2 & 6*/);

  auto b_xor_not_e = SampleBits::B;
  b_xor_not_e ^= ~SampleBits::E;
  EXPECT_EQ(static_cast<uint8_t>(b_xor_not_e), 4u /* 4 ^ 6*/);

  EXPECT_EQ(static_cast<uint8_t>(SampleBits::B), 2u);
  EXPECT_EQ(static_cast<uint8_t>(SampleBits::D), 4u);
  EXPECT_EQ(static_cast<uint8_t>(SampleBits::E), 8u);
}

TEST(Bits, IsConstexpr) {
  using namespace llcpp::fidl::llcpp::types::test;

  static constexpr auto this_should_compile = SampleBits::B | SampleBits::D | SampleBits::E;
  EXPECT_EQ(this_should_compile, SampleBits::mask);
}

TEST(Bits, CanConvertToNumberButMustBeExplicit) {
  using namespace llcpp::fidl::llcpp::types::test;

  uint8_t r8 = static_cast<uint8_t>(SampleBits::B);
  EXPECT_EQ(r8, 2u);
  uint16_t r16 = static_cast<uint8_t>(SampleBits::B);
  EXPECT_EQ(r16, 2u);
}

TEST(Bits, CanConvertToBool) {
  using namespace llcpp::fidl::llcpp::types::test;

  bool result = static_cast<bool>(SampleBits::B);
  EXPECT_TRUE(result);
}
