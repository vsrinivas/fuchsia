// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

#include "error_test.h"
#include "test_library.h"

namespace {

bool GoodNonzeroSizeArray() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8>:1 arr;
};
)FIDL");
  ASSERT_TRUE(library.Compile());

  END_TEST;
}

bool BadZeroSizeArray() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8>:0 arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustHaveNonZeroSize);

  END_TEST;
}

bool BadNoSizeArray() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {
    array<uint8> arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustHaveSize);

  END_TEST;
}

bool BadNonParameterizedArray() {
  BEGIN_TEST;

  TestLibrary library(R"FIDL(
library example;

struct S {
    array arr;
};
)FIDL");
  ASSERT_FALSE(library.Compile());
  const auto& errors = library.errors();
  ASSERT_ERR(errors[0], fidl::ErrMustBeParameterized);

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(array_tests)
RUN_TEST(GoodNonzeroSizeArray)
RUN_TEST(BadZeroSizeArray)
RUN_TEST(BadNoSizeArray)
RUN_TEST(BadNonParameterizedArray)
END_TEST_CASE(array_tests)
