// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/app_test.h"

#include <mutex>

#include "application/lib/app/application_context.h"
#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "gtest/gtest.h"
#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"
#include "lib/mtl/threading/thread.h"

namespace test {

AppTest::AppTest()
    : TestWithMessageLoop(),
      application_context_(
          app::ApplicationContext::CreateFromStartupInfoNotChecked()) {}

AppTest::~AppTest() {}

}  // namespace test

int main(int argc, char** argv) {
  mtl::Thread reporting_thread;

  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);

  reporting_thread.Run();
  reporting_thread.TaskRunner()->PostTask([&reporter] {
    auto context = app::ApplicationContext::CreateFromStartupInfoNotChecked();
    reporter.Start(context.get());
  });

  // Wait until reporter thread has started before continuing. This ensures that
  // the first application context is taken by the reporter. This is necessary
  // because the first instances is the only one that has access to the
  // environment of the caller process.
  {
    std::timed_mutex mutex;
    reporting_thread.TaskRunner()->PostTask(ftl::MakeCopyable(
        [guard =
                std::make_unique<std::lock_guard<std::timed_mutex>>(mutex)]{}));
    FTL_CHECK(mutex.try_lock_for(std::chrono::seconds(1)));
    mutex.unlock();
  }

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  reporting_thread.Join();
  return status;
}
