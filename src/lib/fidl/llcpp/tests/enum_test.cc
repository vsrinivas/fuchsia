// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/llcpp/types/test/llcpp/fidl.h>
#include <gtest/gtest.h>

TEST(Enum, Defaults) {
  using StrictType = llcpp::fidl::llcpp::types::test::StrictEnum;
  using FlexibleType = llcpp::fidl::llcpp::types::test::FlexibleEnum;

  StrictType strict_enum_default = {};
  FlexibleType flexible_enum_default = {};

  EXPECT_EQ(0u, static_cast<uint32_t>(strict_enum_default));
  EXPECT_EQ(0u, static_cast<uint32_t>(flexible_enum_default));
}

TEST(FlexibleEnum, IsUnknown) {
  using EnumType = llcpp::fidl::llcpp::types::test::FlexibleEnum;

  EXPECT_FALSE(EnumType::B.IsUnknown());
  EXPECT_FALSE(EnumType::D.IsUnknown());
  EXPECT_FALSE(EnumType::E.IsUnknown());

  EXPECT_TRUE(EnumType::CUSTOM.IsUnknown());
  EXPECT_TRUE(EnumType::Unknown().IsUnknown());
}

TEST(FlexibleEnum, Equality) {
  using EnumType = llcpp::fidl::llcpp::types::test::FlexibleEnum;

  EXPECT_TRUE(EnumType::B == EnumType(2));
  EXPECT_TRUE(EnumType::D == EnumType(4));
  EXPECT_TRUE(EnumType::Unknown() == EnumType::CUSTOM);

  EXPECT_FALSE(EnumType::B != EnumType(2));
  EXPECT_FALSE(EnumType::D != EnumType(4));
  EXPECT_FALSE(EnumType::Unknown() != EnumType::CUSTOM);
}

TEST(Enum, Switch) {
  using StrictType = llcpp::fidl::llcpp::types::test::StrictEnum;
  using FlexibleType = llcpp::fidl::llcpp::types::test::FlexibleEnum;

  // This is a compilation test ensuring that we can use strict and flexible
  // enums in switch statements.
  auto switch_on_strict = [](StrictType value) -> uint32_t {
    switch (value) {
      case StrictType::B:
        return 4000u;
      case StrictType::D:
        return 5000u;
      case StrictType::E:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_strict(StrictType::E));

  auto switch_on_flexible = [](FlexibleType value) -> uint32_t {
    switch (value) {
      case FlexibleType::B:
        return 4000u;
      case FlexibleType::D:
        return 5000u;
      default:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_flexible(FlexibleType::E));
}
