// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/flat_ast.h>
#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(FlexibleTests, BadEnumMultipleUnknown) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : uint8 {
  @unknown ZERO = 0;
  @unknown ONE = 1;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnMultipleMembers);
}

TEST(FlexibleTests, BadEnumMaxValueWithoutUnknownUnsigned) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : uint8 {
  ZERO = 0;
  ONE = 1;
  MAX = 255;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleTests, BadEnumMaxValueWithoutUnknownSigned) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible enum : int8 {
  ZERO = 0;
  ONE = 1;
  MAX = 127;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrFlexibleEnumMemberWithMaxValue);
}

TEST(FlexibleTests, GoodEnumCanUseMaxValueIfOtherIsUnknownUnsigned) {
  TestLibrary library(R"FIDL(
library example;

flexible enum Foo : uint8 {
  ZERO = 0;
  [Unknown] ONE = 1;
  MAX = 255;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_FALSE(foo_enum->unknown_value_signed.has_value());
  EXPECT_TRUE(foo_enum->unknown_value_unsigned.has_value());
  EXPECT_EQ(foo_enum->unknown_value_unsigned.value(), 1);
}

TEST(FlexibleTests, GoodEnumCanUseMaxValueIfOtherIsUnknownSigned) {
  TestLibrary library(R"FIDL(
library example;

flexible enum Foo : int8 {
  ZERO = 0;
  [Unknown] ONE = 1;
  MAX = 127;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 1);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

TEST(FlexibleTests, GoodEnumCanUseZeroAsUnknownValue) {
  TestLibrary library(R"FIDL(
library example;

flexible enum Foo : int8 {
  [Unknown] ZERO = 0;
  ONE = 1;
  MAX = 127;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto foo_enum = library.LookupEnum("Foo");
  ASSERT_NOT_NULL(foo_enum);
  EXPECT_TRUE(foo_enum->unknown_value_signed.has_value());
  EXPECT_EQ(foo_enum->unknown_value_signed.value(), 0);
  EXPECT_FALSE(foo_enum->unknown_value_unsigned.has_value());
}

TEST(FlexibleTests, GoodUnionWithSingleUnknown) {
  TestLibrary library(R"FIDL(
library example;

flexible union Foo {
  1: int32 a;
  [Unknown] 2: int32 b;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);

  auto foo_union = library.LookupUnion("Foo");
  ASSERT_NOT_NULL(foo_union);
}

TEST(FlexibleTests, BadUnionMultipleUnknown) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type Foo = flexible union {
  @unknown 1: a int32;
  @unknown 2: b int32;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrUnknownAttributeOnMultipleMembers);
}

// TEST(FlexibleTests, BadUnionMaxValueWithoutUnknown) {
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
// }

}  // namespace
