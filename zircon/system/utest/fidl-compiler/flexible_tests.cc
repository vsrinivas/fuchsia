// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(FlexibleEnum, BadMultipleUnknown) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : uint8 {
  [Unknown] ZERO = 0;
  [Unknown] ONE = 1;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnMultipleMembers);
}

TEST(FlexibleEnum, BadMaxValueWithoutUnknownUnsigned) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : uint8 {
  ZERO = 0;
  ONE = 1;
  MAX = 255;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleEnum, BadMaxValueWithoutUnknownSigned) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : int8 {
  ZERO = 0;
  ONE = 1;
  MAX = 127;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleEnum, GoodCanUseMaxValueIfOtherIsUnknownUnsigned) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : uint8 {
  ZERO = 0;
  [Unknown] ONE = 1;
  MAX = 255;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_TRUE(library.Compile());

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_FALSE(foo_enum->unknown_value_signed.has_value());
  EXPECT_TRUE(foo_enum->unknown_value_unsigned.has_value());
  EXPECT_EQ(foo_enum->unknown_value_unsigned.value(), 1);
}

TEST(FlexibleEnum, GoodCanUseMaxValueIfOtherIsUnknownSigned) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : int8 {
  ZERO = 0;
  [Unknown] ONE = 1;
  MAX = 127;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_TRUE(library.Compile());

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 1);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

TEST(FlexibleEnum, GoodCanUseZeroAsUnknownValue) {
  std::string fidl_library = R"FIDL(
library example;

flexible enum Foo : int8 {
  [Unknown] ZERO = 0;
  ONE = 1;
  MAX = 127;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_TRUE(library.Compile());

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 0);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

TEST(FlexibleUnion, BadMultipleUnknown) {
  std::string fidl_library = R"FIDL(
library example;

flexible union Foo {
  [Unknown] 1: int32 a;
  [Unknown] 2: int32 b;
};
)FIDL";

  TestLibrary library(fidl_library);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnMultipleMembers);
}

TEST(FlexibleUnion, BadMaxValueWithoutUnknown) {
  // Ideally, we'd want to be able to define a union with an ordinal that's the
  // maximum possible value for a uint64:
  //
  // flexible union Foo {
  //   1: reserved;
  //   2: reserved;
  //   3: reserved;
  //   …
  //   UINT64_MAX: int32 a;
  // };
  //
  // … and ensure that this fails compilation, due to UINT64_MAX being reserved
  // for the unknown member. However, it's impossible to define this given that
  // union ordinals must be contiguous (the disk space used for the FIDL definition
  // in ASCII would require 18 petabytes), so it doesn't make sense to test for this.
}

}  // namespace
