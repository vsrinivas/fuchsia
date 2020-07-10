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
  ASSERT_TRUE(library.Compile());
}

TEST(ArrayTests, BadZeroSizeArray) {
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

TEST(ArrayTests, BadNoSizeArray) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8> arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustHaveSize);
}

TEST(ArrayTests, BadNonParameterizedArray) {
  TestLibrary library(R"FIDL(
library example;

struct S {
    array arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);
}

}  // namespace
