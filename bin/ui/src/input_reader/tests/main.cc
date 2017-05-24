// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  return mozart::test::RunTestsWithMessageLoopAndTestRunner(
      "mozart_input_tests", [](auto) { return RUN_ALL_TESTS(); });
}
