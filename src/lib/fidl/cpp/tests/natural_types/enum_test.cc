// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/natural_types.h>

#include <gtest/gtest.h>

using test_types::EmptyFlexibleEnum;
using test_types::FlexibleEnum;
using test_types::StrictEnum;

TEST(Enum, Defaults) {
  using StrictType = StrictEnum;
  using FlexibleType = FlexibleEnum;

  StrictType strict_enum_default = {};
  FlexibleType flexible_enum_default = {};

  EXPECT_EQ(0u, static_cast<uint32_t>(strict_enum_default));
  EXPECT_EQ(56u, static_cast<uint32_t>(flexible_enum_default));
  EXPECT_TRUE(flexible_enum_default.IsUnknown());
}

TEST(FlexibleEnum, IsUnknown) {
  using EnumType = FlexibleEnum;

  EXPECT_FALSE(EnumType::kB.IsUnknown());
  EXPECT_FALSE(EnumType::kD.IsUnknown());
  EXPECT_FALSE(EnumType::kE.IsUnknown());

  EXPECT_TRUE(EnumType::kCustom.IsUnknown());
  EXPECT_TRUE(EnumType::Unknown().IsUnknown());
}

TEST(EmptyFlexibleEnum, IsUnknown) {
  using EnumType = EmptyFlexibleEnum;

  EXPECT_TRUE(EnumType::Unknown().IsUnknown());
}

TEST(FlexibleEnum, Equality) {
  using EnumType = FlexibleEnum;

  EXPECT_TRUE(EnumType::kB == EnumType(2));
  EXPECT_TRUE(EnumType::kD == EnumType(4));
  EXPECT_TRUE(EnumType::Unknown() == EnumType::kCustom);

  EXPECT_FALSE(EnumType::kB != EnumType(2));
  EXPECT_FALSE(EnumType::kD != EnumType(4));
  EXPECT_FALSE(EnumType::Unknown() != EnumType::kCustom);
}

TEST(Enum, Switch) {
  using StrictType = StrictEnum;
  using FlexibleType = FlexibleEnum;

  // This is a compilation test ensuring that we can use strict and flexible
  // enums in switch statements.
  auto switch_on_strict = [](StrictType value) -> uint32_t {
    switch (value) {
      case StrictType::kB:
        return 4000u;
      case StrictType::kD:
        return 5000u;
      case StrictType::kE:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_strict(StrictType::kE));

  auto switch_on_flexible = [](FlexibleType value) -> uint32_t {
    switch (value) {
      case FlexibleType::kB:
        return 4000u;
      case FlexibleType::kD:
        return 5000u;
      default:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_flexible(FlexibleType::kE));
}

TEST(Enum, Clone) {
  StrictEnum se1 = StrictEnum::kB;
  EXPECT_EQ(se1, StrictEnum::kB);
  StrictEnum se2 = se1;
  EXPECT_EQ(se1, se2);

  FlexibleEnum fe1 = FlexibleEnum::kB;
  EXPECT_EQ(fe1, FlexibleEnum::kB);
  FlexibleEnum fe2 = fe1;
  EXPECT_EQ(fe1, fe2);
}
