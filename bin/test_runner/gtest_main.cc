// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/reporting/gtest_listener.h"
#include "apps/test_runner/lib/reporting/reporter.h"
#include "apps/test_runner/lib/reporting/results_queue.h"
#include "gtest/gtest.h"
#include "lib/mtl/threading/thread.h"

int main(int argc, char** argv) {
  mtl::Thread reporting_thread;

  test_runner::ResultsQueue queue;
  test_runner::Reporter reporter(argv[0], &queue);
  test_runner::GTestListener listener(argv[0], &queue);

  reporting_thread.Run();
  reporting_thread.TaskRunner()->PostTask([&reporter] {
    reporter.Start();
  });

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&listener);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&listener);

  reporting_thread.Join();
  return status;
}
