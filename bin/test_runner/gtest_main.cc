// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/gtest_reporter.h"
#include "gtest/gtest.h"

int main(int argc, char** argv) {
  test_runner::GoogleTestReporter reporter(argv[0]);

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&reporter);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&reporter);
  return status;
}
