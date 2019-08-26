// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <unittest/unittest.h>

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
  auto errors = library.errors();
  ASSERT_STR_STR(errors[0].data(), "must have non-zero size");

  END_TEST;
}

}  // namespace

BEGIN_TEST_CASE(array_tests)
RUN_TEST(GoodNonzeroSizeArray)
RUN_TEST(BadZeroSizeArray)
END_TEST_CASE(array_tests)
