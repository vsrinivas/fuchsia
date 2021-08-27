// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

TEST(ArrayTests, GoodNonzeroSizeArray) {
  TestLibrary library(R"FIDL(library example;

type S = struct {
    arr array<uint8, 1>;
};
)FIDL");
  ASSERT_COMPILED(library);
}

TEST(ArrayTests, BadZeroSizeArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8,0>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveNonZeroSize);
}

TEST(ArrayTests, BadNoSizeArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8>;
};
)FIDL");
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadNonParameterizedArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array;
};
)FIDL");
  // NOTE(fxbug.dev/72924): A more general error is thrown in the new syntax
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadOptionalArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeNullable);
}

TEST(ArrayTest, BadMultipleConstraintsOnArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:<optional, foo, bar>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

}  // namespace
