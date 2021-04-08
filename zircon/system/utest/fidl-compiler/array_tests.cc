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

TEST(ArrayTests, BadZeroSizeArrayOld) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8>:0 arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustHaveNonZeroSize);
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

TEST(ArrayTests, BadNoSizeArrayOld) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8> arr;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveSize);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustHaveSize);
}

TEST(ArrayTests, BadNonParameterizedArrayOld) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array arr;
};
)FIDL");
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeParameterized);
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
  ASSERT_ERRORED_DURING_COMPILE(library, fidl::ErrMustBeParameterized);
}

}  // namespace
