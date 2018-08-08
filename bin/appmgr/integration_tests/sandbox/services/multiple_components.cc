// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/appmgr/integration_tests/sandbox/namespace_test.h"

#include <lib/component/cpp/termination_reason.h>
#include <stdio.h>
#include <unistd.h>
#include <vector>

#include <fuchsia/sys/cpp/fidl.h>
#include <zircon/syscalls.h>
#include "gtest/gtest.h"
#include "lib/component/cpp/testing/test_util.h"

// This test runs multiple components in the same environment, and checks that
// their service sandboxes are isolated.
TEST_F(NamespaceTest, MultipleComponents) {
  static const std::vector<const char*> kTests = {"some_services",
                                                  "all_services"};

  int num_running = kTests.size();
  std::vector<fuchsia::sys::ComponentControllerPtr> controllers;
  for (const char* test_url : kTests) {
    fuchsia::sys::LaunchInfo launch_info;
    launch_info.url = test_url;
    fuchsia::sys::ComponentControllerPtr controller;
    CreateComponentInCurrentEnvironment(std::move(launch_info),
                                        controller.NewRequest());
    controller.events().OnTerminated =
        [test_url, &num_running](
            int64_t return_code,
            fuchsia::sys::TerminationReason termination_reason) {
          EXPECT_EQ(return_code, 0) << test_url << " exited with non-ok status";
          EXPECT_EQ(termination_reason, fuchsia::sys::TerminationReason::EXITED)
              << test_url << " exited unexpectedly";
          --num_running;
        };
    controllers.push_back(std::move(controller));
  }

  ASSERT_TRUE(RunLoopWithTimeoutOrUntil(
      [&num_running] { return num_running == 0; }, zx::sec(10)));
  EXPECT_EQ(num_running, 0);
}