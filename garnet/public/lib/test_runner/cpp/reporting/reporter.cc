// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/reporting/reporter.h"

#include <fuchsia/testing/runner/cpp/fidl.h>
#include <lib/fidl/cpp/synchronous_interface_ptr.h>
#include <lib/sys/cpp/startup_context.h>

using fuchsia::testing::runner::TestRunnerSyncPtr;

namespace test_runner {

void ReportResult(std::string identity, sys::StartupContext* context,
                  std::vector<TestResultPtr> results) {
  TestRunnerSyncPtr test_runner;
  context->svc()->Connect(test_runner.NewRequest());

  test_runner->Identify(identity);
  for (auto& result : results) {
    test_runner->ReportResult(std::move(*result));
  }
  test_runner->Teardown();
}

}  // namespace test_runner
