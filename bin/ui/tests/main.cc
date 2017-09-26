// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/ui/tests/test_with_message_loop.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"
#include "gtest/gtest.h"

std::unique_ptr<app::ApplicationContext> g_application_context;

int main(int argc, char** argv) {
  test_runner::GTestListener listener(argv[0]);

  fsl::MessageLoop message_loop;
  g_application_context =
      app::ApplicationContext::CreateFromStartupInfoNotChecked();

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);

  int status = RUN_ALL_TESTS();

  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  test_runner::ReportResult(argv[0], g_application_context.get(), listener.GetResults());

  return status;
}
