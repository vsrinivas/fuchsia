// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/test_runner/cpp/reporting/reporter.h"

#include "lib/app/cpp/application_context.h"
#include "lib/test_runner/fidl/test_runner.fidl-sync.h"
#include "lib/fidl/cpp/bindings/synchronous_interface_ptr.h"

namespace test_runner {

void ReportResult(std::string identity,
                  app::ApplicationContext* context,
                  std::vector<TestResultPtr> results) {
  if (!context->has_environment_services()) {
    return;
  }

  fidl::SynchronousInterfacePtr<TestRunner> test_runner;
  context->ConnectToEnvironmentService(fidl::GetSynchronousProxy(&test_runner));

  test_runner->Identify(identity);
  for (auto& result : results) {
    test_runner->ReportResult(std::move(result));
  }
  test_runner->Teardown();
}

}  // namespace test_runner
