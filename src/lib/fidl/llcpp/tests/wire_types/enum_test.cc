// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test.types/cpp/wire.h>

#include <gtest/gtest.h>

using test_types::wire::EmptyFlexibleEnum;
using test_types::wire::FlexibleEnum;
using test_types::wire::StrictEnum;

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

TEST(StrictEnum, DisplayErrorTypeTruncated) {
  StrictEnum e = StrictEnum::kB;
  constexpr size_t kTinyCapacity = 10;
  char buffer[kTinyCapacity];
  size_t written = fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer, written), "test.type");
  EXPECT_EQ(kTinyCapacity - 1, written);
  EXPECT_EQ('\0', buffer[kTinyCapacity - 1]);
}

TEST(StrictEnum, DisplayErrorValueTruncated) {
  StrictEnum e = StrictEnum::kB;
  constexpr size_t kSmallCapacity = 26;
  char buffer[kSmallCapacity];
  size_t written = fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer, written), "test.types/StrictEnum.B (");
  EXPECT_EQ(kSmallCapacity - 1, written);
  EXPECT_EQ('\0', buffer[kSmallCapacity - 1]);
}

TEST(StrictEnum, KnownDisplayError) {
  StrictEnum e = StrictEnum::kB;
  char buffer[100];
  fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer), "test.types/StrictEnum.B (value: 2)");
}

TEST(StrictEnum, UnknownDisplayError) {
  StrictEnum e = static_cast<StrictEnum>(0);
  char buffer[100];
  fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer), "test.types/StrictEnum.[UNKNOWN] (value: 0)");
}

TEST(FlexibleEnum, KnownDisplayError) {
  FlexibleEnum e = FlexibleEnum::kB;
  char buffer[100];
  fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer), "test.types/FlexibleEnum.B (value: 2)");
}

TEST(FlexibleEnum, UnknownDisplayError) {
  FlexibleEnum e = static_cast<FlexibleEnum>(0);
  char buffer[100];
  fidl::internal::FormatDisplayError(e, buffer, sizeof(buffer));
  EXPECT_EQ(std::string_view(buffer), "test.types/FlexibleEnum.[UNKNOWN] (value: 0)");
}
