// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <zxtest/zxtest.h>

#include "tools/fidl/fidlc/include/fidl/diagnostics.h"
#include "tools/fidl/fidlc/tests/error_test.h"
#include "tools/fidl/fidlc/tests/test_library.h"

// This file is meant to hold standalone tests for each of the "good" examples used in the documents
// at //docs/reference/fidl/language/error-catalog. These cases are redundant with the other tests
// in this suite - their purpose is not to serve as tests for the features at hand, but rather to
// provide well-vetted and tested examples of the "correct" way to fix FIDL errors.

namespace {

TEST(ErrcatTests, Good0003) {
  TestLibrary library;
  library.AddFile("good/fi-0003.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0012) {
  TestLibrary library;
  library.AddFile("good/fi-0012.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0028A) {
  TestLibrary library;
  library.AddFile("good/fi-0028-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0046) {
  TestLibrary library;
  library.AddFile("good/fi-0046.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0071A) {
  TestLibrary library;
  library.AddFile("good/fi-0071-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0071B) {
  TestLibrary library;
  library.AddFile("good/fi-0071-b.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0072A) {
  TestLibrary library;
  library.AddFile("good/fi-0072-a.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0072B) {
  TestLibrary library;
  library.AddFile("good/fi-0072-b.test.fidl");
  ASSERT_COMPILED(library);
}

TEST(ErrcatTests, Good0110B) {
  TestLibrary library;
  library.AddFile("good/fi-0110-b.test.fidl");
  ASSERT_COMPILED(library);
}

}  // namespace
