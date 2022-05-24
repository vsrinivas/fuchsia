// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>

#include <gtest/gtest.h>

TEST(BitsConversion, StrictBitsToNatural) {
  test::types::StrictBits hlcpp = test::types::StrictBits::D | test::types::StrictBits::E;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::StrictBits>);
  EXPECT_EQ(natural, test_types::StrictBits::kD | test_types::StrictBits::kE);
}

TEST(BitsConversion, StrictBitsToHLCPP) {
  test_types::StrictBits natural = test_types::StrictBits::kD | test_types::StrictBits::kE;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::StrictBits>);
  EXPECT_EQ(hlcpp, test::types::StrictBits::D | test::types::StrictBits::E);
}

TEST(BitsConversion, FlexibleBitsToNatural) {
  test::types::FlexibleBits hlcpp = test::types::FlexibleBits::D | test::types::FlexibleBits::E;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::FlexibleBits>);
  EXPECT_EQ(natural, test_types::FlexibleBits::kD | test_types::FlexibleBits::kE);
}

TEST(BitsConversion, FlexibleBitsToHLCPP) {
  test_types::FlexibleBits natural = test_types::FlexibleBits::kD | test_types::FlexibleBits::kE;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::FlexibleBits>);
  EXPECT_EQ(hlcpp, test::types::FlexibleBits::D | test::types::FlexibleBits::E);
}
