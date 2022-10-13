// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/include/fidl/flat_ast.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

namespace {

TEST(FlexibleTests, BadEnumMultipleUnknown) {
  TestLibrary library;
  library.AddFile("bad/fi-0072.test.fidl");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnMultipleEnumMembers);
}

TEST(FlexibleTests, BadEnumMaxValueWithoutUnknownUnsigned) {
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : uint8 {
  ZERO = 0;
  ONE = 1;
  MAX = 255;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleTests, BadEnumMaxValueWithoutUnknownSigned) {
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : int8 {
  ZERO = 0;
  ONE = 1;
  MAX = 127;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleTests, GoodEnumCanUseMaxValueIfOtherIsUnknownUnsigned) {
  TestLibrary library(R"FIDL(library example;

type Foo = flexible enum : uint8 {
    ZERO = 0;
    @unknown
    ONE = 1;
    MAX = 255;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_FALSE(foo_enum->unknown_value_signed.has_value());
  EXPECT_TRUE(foo_enum->unknown_value_unsigned.has_value());
  EXPECT_EQ(foo_enum->unknown_value_unsigned.value(), 1);
}

TEST(FlexibleTests, GoodEnumCanUseMaxValueIfOtherIsUnknownSigned) {
  TestLibrary library(R"FIDL(library example;

type Foo = flexible enum : int8 {
    ZERO = 0;
    @unknown
    ONE = 1;
    MAX = 127;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 1);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

TEST(FlexibleTests, GoodEnumCanUseZeroAsUnknownValue) {
  TestLibrary library(R"FIDL(library example;

type Foo = flexible enum : int8 {
    @unknown
    ZERO = 0;
    ONE = 1;
    MAX = 127;
};
)FIDL");
  ASSERT_COMPILED(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 0);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

}  // namespace
