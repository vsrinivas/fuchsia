// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/mozart/services/views/view_manager.fidl.h"
#include "gtest/gtest.h"

mozart::ViewManagerPtr g_view_manager;

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);

  auto run_tests = [](app::ApplicationContext* application_context) {
    auto view_manager =
        application_context->ConnectToEnvironmentService<mozart::ViewManager>();
    g_view_manager = mozart::ViewManagerPtr::Create(std::move(view_manager));
    return RUN_ALL_TESTS();
  };
  return mozart::test::RunTestsWithMessageLoopAndTestRunner(
      "mozart_view_manager_tests", run_tests);
}
