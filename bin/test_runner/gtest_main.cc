// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/gtest_reporter.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  test_runner::GoogleTestReporter reporter(argv[0]);
  return RUN_ALL_TESTS();
}
