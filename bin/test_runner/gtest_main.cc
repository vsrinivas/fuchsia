// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "apps/test_runner/lib/application_context.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "gtest/gtest.h"
#include "lib/mtl/threading/thread.h"

int main(int argc, char** argv) {
  test_runner::Reporter reporter(argv[0]);
  test_runner::GTestListener listener(argv[0], &reporter);

  reporter.Start(test_runner::GetApplicationContext());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  return status;
}
