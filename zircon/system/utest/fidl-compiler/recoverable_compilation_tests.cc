// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "fidl/diagnostics.h"
#include "test_library.h"

namespace {

TEST(RecoverableCompilationTests, BadRecoverInLibraryConsume) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

protocol P {};
protocol P {};              // Error: name collision

@foo
@foo("foo")                 // Error: attribute name collision
type Foo = struct {};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 2);
  EXPECT_ERR(errors[0], fidl::ErrNameCollision);
  EXPECT_ERR(errors[1], fidl::ErrDuplicateAttribute);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryCompile) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
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
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 4);
  ASSERT_ERR(errors[0], fidl::ErrDuplicateMemberValue);
  ASSERT_ERR(errors[1], fidl::ErrNonDenseOrdinal);
  ASSERT_ERR(errors[2], fidl::ErrDuplicateMemberName);
  ASSERT_ERR(errors[3], fidl::ErrUnknownType);
}

TEST(RecoverableCompilationTests, BadRecoverInLibraryVerifyAttributes) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

@for_deprecated_c_bindings("True")  // Error: invalid placement & value
type Union = union {
    1: foo string;
};

@transitional                       // Error: invalid placement
type Table = table {
    1: foo string;
};

@max_bytes("1")                     // Error: too large
type Struct = struct {
    foo uint16;
};
)FIDL",
                      experimental_flags);
  EXPECT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_EQ(errors.size(), 4);
  ASSERT_ERR(errors[0], fidl::ErrInvalidAttributePlacement);
  ASSERT_ERR(errors[1], fidl::ErrInvalidAttributeValue);
  ASSERT_ERR(errors[2], fidl::ErrInvalidAttributePlacement);
  ASSERT_ERR(errors[3], fidl::ErrTooManyBytes);
}

}  // namespace
