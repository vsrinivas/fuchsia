// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async-loop/cpp/loop.h>

#include "gtest/gtest.h"
#include "lib/component/cpp/startup_context.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  {
    async::Loop loop(&kAsyncLoopConfigMakeDefault);
    auto context = component::StartupContext::CreateFromStartupInfo();
    test_runner::ReportResult(argv[0], context.get(), listener.GetResults());
  }

  return status;
}
