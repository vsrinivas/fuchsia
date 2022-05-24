// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/hlcpp_conversion.h>

#include <gtest/gtest.h>

TEST(EnumConversion, StrictEnumToNatural) {
  test::types::StrictEnum hlcpp = test::types::StrictEnum::D;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::StrictEnum>);
  EXPECT_EQ(natural, test_types::StrictEnum::kD);
}

TEST(EnumConversion, StrictEnumToHLCPP) {
  test_types::StrictEnum natural = test_types::StrictEnum::kD;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::StrictEnum>);
  EXPECT_EQ(hlcpp, test::types::StrictEnum::D);
}

TEST(EnumConversion, FlexibleEnumToNatural) {
  test::types::FlexibleEnum hlcpp = test::types::FlexibleEnum::D;
  auto natural = fidl::HLCPPToNatural(hlcpp);
  static_assert(std::is_same_v<decltype(natural), test_types::FlexibleEnum>);
  EXPECT_EQ(natural, test_types::FlexibleEnum::kD);
}

TEST(EnumConversion, FlexibleEnumToHLCPP) {
  test_types::FlexibleEnum natural = test_types::FlexibleEnum::kD;
  auto hlcpp = fidl::NaturalToHLCPP(natural);
  static_assert(std::is_same_v<decltype(hlcpp), test::types::FlexibleEnum>);
  EXPECT_EQ(hlcpp, test::types::FlexibleEnum::D);
}
