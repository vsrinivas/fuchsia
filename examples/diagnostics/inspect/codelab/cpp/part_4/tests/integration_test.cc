// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/diagnostics/cpp/fidl.h>

#include <examples/diagnostics/inspect/codelab/cpp/testing/integration_test.h>

class IntegrationTestPart4 : public codelab::testing::IntegrationTest {};

// [START integration_test]
TEST_F(IntegrationTestPart4, StartWithFizzBuzz) {
  auto ptr = ConnectToReverser({.include_fizzbuzz = true});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);

  // CODELAB: Check that the component was connected to FizzBuzz.
}
// [END integration_test]

TEST_F(IntegrationTestPart4, StartWithoutFizzBuzz) {
  auto ptr = ConnectToReverser({.include_fizzbuzz = false});

  bool error = false;
  ptr.set_error_handler([&](zx_status_t unused) { error = true; });

  bool done = false;
  std::string result;
  ptr->Reverse("hello", [&](std::string value) {
    result = std::move(value);
    done = true;
  });
  RunLoopUntil([&] { return done || error; });

  ASSERT_FALSE(error);
  EXPECT_EQ("olleh", result);

  // CODELAB: Check that the component failed to connect to FizzBuzz.
}
