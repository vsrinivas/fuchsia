// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(ArrayTests, GoodNonzeroSizeArray) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8>:1 arr;
};
)FIDL");
  ASSERT_COMPILED_AND_CONVERT(library);
}

TEST(ArrayTests, BadZeroSizeArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8,0>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveNonZeroSize);
}

TEST(ArrayTests, BadNoSizeArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8>;
};
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadNonParameterizedArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array;
};
)FIDL",
                      experimental_flags);
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadOptionalArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:optional;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(ArrayTest, BadMultipleConstraintsOnArray) {
  fidl::ExperimentalFlags experimental_flags;
  experimental_flags.SetFlag(fidl::ExperimentalFlags::Flag::kAllowNewSyntax);
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:<optional, foo, bar>;
};
)FIDL",
                      experimental_flags);
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

}  // namespace
