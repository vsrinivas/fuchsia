// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/app/cpp/application_context.h"
#include "lib/ui/tests/test_with_message_loop.h"
#include "garnet/bin/ui/scene_manager/tests/escher_test_environment.h"
#include "garnet/bin/ui/scene_manager/tests/session_test.h"
#include "lib/test_runner/cpp/reporting/gtest_listener.h"
#include "lib/test_runner/cpp/reporting/reporter.h"
#include "gtest/gtest.h"
#include "lib/escher/examples/common/demo_harness.h"

std::unique_ptr<scene_manager::test::EscherTestEnvironment> g_escher_env;

int main(int argc, char** argv) {
  // Add a global environment which will set up (and tear down) DemoHarness and
  // Escher. This also implicitly creates a message loop.
  g_escher_env = std::make_unique<scene_manager::test::EscherTestEnvironment>();
  g_escher_env->SetUp(argv[0]);

  // TestRunner setup. Copied from //garent/public/lib/test_runner/cpp/gtest_main.cc.
  test_runner::Reporter reporter(argv[0]);
  test_runner::GTestListener listener(argv[0], &reporter);

  auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
  reporter.Start(context.get());

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  g_escher_env->TearDown();
  return status;
}
