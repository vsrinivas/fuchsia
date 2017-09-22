// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/app_test.h"

#include "peridot/bin/ledger/test/get_ledger.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fxl/logging.h"

namespace test {

int TestMain(int argc, char** argv) {
  test_runner::Reporter reporter(argv[0]);
  test_runner::GTestListener listener(argv[0], &reporter);

  auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
  reporter.Start(context.get());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  return status;
}

}  // namespace test
