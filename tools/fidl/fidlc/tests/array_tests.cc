// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadNonParameterizedArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrWrongNumberOfLayoutParameters);
}

TEST(ArrayTests, BadOptionalArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:optional;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrCannotBeOptional);
}

TEST(ArrayTest, BadMultipleConstraintsOnArray) {
  TestLibrary library(R"FIDL(
library example;

type S = struct {
    arr array<uint8, 10>:<optional, 1, 2>;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrTooManyConstraints);
}

}  // namespace
