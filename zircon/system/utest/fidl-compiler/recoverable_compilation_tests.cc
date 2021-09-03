// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(RecoverableCompilationTests, BadRecoverInLibraryConsume) {
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol P {};              // Error: name collision

@foo
@foo("foo")                 // Error: attribute name collision
type Foo = struct {};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  EXPECT_ERR(errors[0], fidl::ErrNameCollision);
  EXPECT_ERR(errors[1], fidl::ErrDuplicateAttribute);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryCompile) {
  TestLibrary library(R"FIDL(
library example;

type Union = union {
    1: string_value string;
    2: unknown_value UnknownType; // Error: unknown type
};

type Enum = enum {
    ZERO = 0;
    ONE = 1;
    TWO = 1;                      // Error: duplicate value
    THREE = 3;
};

type OtherEnum = enum {
    NONE = 0;
    ONE = 1;
    ONE = 2;                      // Error: duplicate name
};

type NonDenseTable = table {
    1: s string;
    3: b uint8;                   // Error: non-dense ordinals
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 4);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_ERR(errors[1], fidl::ErrNonDenseOrdinal);
  ASSERT_ERR(errors[2], fidl::ErrDuplicateMemberName);
  ASSERT_ERR(errors[3], fidl::ErrUnknownType);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryVerifyAttributePlacement) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@transitional            // Error: invalid placement
type Table = table {
    1: foo string;
};

@max_bytes("1")          // Error: too large
type Struct = struct {
    foo uint16;
};
)FIDL");
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  ASSERT_ERR(errors[0], fidl::ErrInvalidAttributePlacement);
  ASSERT_ERR(errors[1], fidl::ErrTooManyBytes);
}

TEST(RecoverableCompilationTests, BadRecoverInAttributeCompile) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kNewSyntaxOnly);
  TestLibrary library(R"FIDL(
library example;

@foo(first="a", first="b")   // Error: duplicate args
@bar(first=3, second=4)      // Error: x2 cannot use numeric args on custom attributes
type Enum = enum {
    FOO = 1;
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 3);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateAttributeArg);
  ASSERT_ERR(errors[1], fidl::ErrCannotUseNumericArgsOnCustomAttributes);
  ASSERT_ERR(errors[2], fidl::ErrCannotUseNumericArgsOnCustomAttributes);
}

}  // namespace
