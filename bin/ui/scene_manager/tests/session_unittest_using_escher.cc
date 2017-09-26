// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/tests/escher_test_environment.h"
#include "garnet/bin/ui/scene_manager/tests/session_test.h"
#include "garnet/examples/escher/common/demo_harness.h"
#include "gtest/gtest.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fsl/tasks/message_loop.h"
#include "lib/test_runner/cpp/application_context.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"
#include "lib/ui/tests/test_with_message_loop.h"

std::unique_ptr<scene_manager::test::EscherTestEnvironment> g_escher_env;

int main(int argc, char** argv) {
  // Add a global environment which will set up (and tear down) DemoHarness and
  // Escher. This also implicitly creates a message loop.
  g_escher_env = std::make_unique<scene_manager::test::EscherTestEnvironment>();
  g_escher_env->SetUp(argv[0]);

  // TestRunner setup. Copied from //garent/public/lib/test_runner/cpp/gtest_main.cc.
  test_runner::GTestListener listener(argv[0]);

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
  test_runner::ReportResult(argv[0], context.get(), listener.GetResults());

  g_escher_env->TearDown();
  return status;
}
