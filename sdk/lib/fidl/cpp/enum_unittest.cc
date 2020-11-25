// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/test/misc/cpp/fidl.h>
#include <zxtest/zxtest.h>

#include "lib/fidl/cpp/builder.h"

namespace fidl {
namespace test {
namespace misc {
namespace {

TEST(Enum, Defaults) {
  StrictEnum strict_enum_default = {};
  FlexibleEnum flexible_enum_default = {};

  EXPECT_EQ(0u, static_cast<uint32_t>(strict_enum_default));
  EXPECT_EQ(0u, static_cast<uint32_t>(flexible_enum_default));
}

TEST(Enum, IsUnknowm) {
  EXPECT_FALSE(FlexibleEnum::MEMBER_A.IsUnknown());
  EXPECT_FALSE(FlexibleEnum::MEMBER_B.IsUnknown());
  EXPECT_FALSE(FlexibleEnum::MEMBER_C.IsUnknown());
  EXPECT_TRUE(FlexibleEnum::MEMBER_CUSTOM_UNKNOWN.IsUnknown());
  EXPECT_TRUE(FlexibleEnum::Unknown().IsUnknown());
}

TEST(Enum, Equality) {
  EXPECT_TRUE(FlexibleEnum::MEMBER_A == FlexibleEnum(23));
  EXPECT_TRUE(FlexibleEnum::MEMBER_B == FlexibleEnum(34));
  EXPECT_TRUE(FlexibleEnum::MEMBER_C == FlexibleEnum(45));
  EXPECT_TRUE(FlexibleEnum::Unknown() == FlexibleEnum::MEMBER_CUSTOM_UNKNOWN);

  EXPECT_FALSE(FlexibleEnum::MEMBER_A != FlexibleEnum(23));
  EXPECT_FALSE(FlexibleEnum::MEMBER_B != FlexibleEnum(34));
  EXPECT_FALSE(FlexibleEnum::MEMBER_C != FlexibleEnum(45));
  EXPECT_FALSE(FlexibleEnum::Unknown() != FlexibleEnum::MEMBER_CUSTOM_UNKNOWN);
}

TEST(Enum, Switch) {
  // This is a compilation test ensuring that we can use strict and flexible
  // enums in switch statements.
  auto switch_on_strict = [](StrictEnum value) -> uint32_t {
    switch (value) {
      case StrictEnum::MEMBER_A:
        return 4000u;
      case StrictEnum::MEMBER_B:
        return 5000u;
      case StrictEnum::MEMBER_C:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_strict(StrictEnum::MEMBER_C));

  auto switch_on_flexible = [](FlexibleEnum value) -> uint32_t {
    switch (value) {
      case FlexibleEnum::MEMBER_A:
        return 4000u;
      case FlexibleEnum::MEMBER_B:
        return 5000u;
      default:
        return 6000u;
    }
  };
  EXPECT_EQ(6000u, switch_on_flexible(FlexibleEnum::MEMBER_C));
}

}  // namespace
}  // namespace misc
}  // namespace test
}  // namespace fidl
