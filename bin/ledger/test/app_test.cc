// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/app_test.h"

#include "application/lib/app/application_context.h"
#include "apps/ledger/src/callback/synchronous_task.h"
#include "apps/ledger/src/test/get_ledger.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/threading/thread.h"

namespace test {

AppTest::AppTest()
    : application_context_(
          app::ApplicationContext::CreateFromStartupInfoNotChecked()) {}

AppTest::~AppTest() {}

int TestMain(int argc, char** argv) {
  mtl::Thread reporting_thread;

  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);

  reporting_thread.Run();
  // Wait until reporter thread has started before continuing. This ensures that
  // the first application context is taken by the reporter. This is necessary
  // because the first instances is the only one that has access to the
  // environment of the caller process.
  FTL_CHECK(callback::RunSynchronously(
      reporting_thread.TaskRunner(),
      [&reporter] {
        auto context =
            app::ApplicationContext::CreateFromStartupInfoNotChecked();
        reporter.Start(context.get());
      },
      ftl::TimeDelta::FromSeconds(1)));

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  reporting_thread.Join();
  return status;
}

}  // namespace test
