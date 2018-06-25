// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/reporting/reporter.h"

#include <fuchsia/testing/runner/cpp/fidl.h>

#include "lib/app/cpp/startup_context.h"
#include "lib/fidl/cpp/synchronous_interface_ptr.h"

using fuchsia::testing::runner::TestRunnerSync2Ptr;

namespace test_runner {

void ReportResult(std::string identity, fuchsia::sys::StartupContext* context,
                  std::vector<TestResultPtr> results) {
  if (!context->has_environment_services()) {
    return;
  }

  TestRunnerSync2Ptr test_runner;
  context->ConnectToEnvironmentService(test_runner.NewRequest());

  test_runner->Identify(identity);
  for (auto& result : results) {
    test_runner->ReportResult(std::move(*result));
  }
  test_runner->Teardown();
}

}  // namespace test_runner
