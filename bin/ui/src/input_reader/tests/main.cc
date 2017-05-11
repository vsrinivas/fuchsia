// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  mtl::MessageLoop message_loop;

  auto application_context = app::ApplicationContext::CreateFromStartupInfo();

  // TODO(vardhan,bgoldman): These tests shouldn't have to deal with this.
  // figure out how to hide all of this.
  test_runner::TestRunnerPtr test_runner =
      application_context
          ->ConnectToEnvironmentService<test_runner::TestRunner>();
  test_runner->Identify("mozart_input_tests");
  int status = RUN_ALL_TESTS();
  if (status != 0) {
    test_runner->Fail("Failed");
  }
  test_runner->Teardown([] {
    mtl::MessageLoop::GetCurrent()->PostQuitTask();
  });
  message_loop.Run();
  return status;
}
