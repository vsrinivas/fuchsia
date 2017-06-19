// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/mozart/lib/tests/test_with_message_loop.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "gtest/gtest.h"
#include "lib/mtl/threading/thread.h"

std::unique_ptr<app::ApplicationContext> g_application_context;

int main(int argc, char** argv) {
  mtl::Thread reporting_thread;

  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);

  g_application_context =
      app::ApplicationContext::CreateFromStartupInfoNotChecked();

  reporting_thread.Run();
  reporting_thread.TaskRunner()->PostTask([&reporter] {
    reporter.Start(g_application_context.get());
  });

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = mozart::test::RunTestsWithMessageLoop([] {
    return RUN_ALL_TESTS();
  });
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  reporting_thread.Join();
  return status;
}
