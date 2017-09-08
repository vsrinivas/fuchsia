// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "lib/ui/tests/test_with_message_loop.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "gtest/gtest.h"

std::unique_ptr<app::ApplicationContext> g_application_context;

int main(int argc, char** argv) {
  test_runner::Reporter reporter(argv[0]);
  test_runner::GTestListener listener(argv[0], &reporter);

  g_application_context =
      app::ApplicationContext::CreateFromStartupInfoNotChecked();

  reporter.Start(g_application_context.get());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);

  mtl::MessageLoop message_loop;
  int status = RUN_ALL_TESTS();

  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  return status;
}
