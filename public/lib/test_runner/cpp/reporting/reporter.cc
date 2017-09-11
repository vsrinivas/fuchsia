// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/reporting/reporter.h"

#include "lib/app/cpp/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"

namespace test_runner {

Reporter::Reporter(std::string identity) : identity_(std::move(identity)) {}

Reporter::~Reporter() {
  Stop();
}

void Reporter::Report(TestResultPtr result) {
  test_runner_->ReportResult(std::move(result));
}

void Reporter::Start(app::ApplicationContext* context) {
  if (context->has_environment_services()) {
    auto test_runner_request = fidl::GetSynchronousProxy(&test_runner_);
    context->ConnectToEnvironmentService(std::move(test_runner_request));

    test_runner_->Identify(identity_);
  }
}

void Reporter::Stop() {
  if (test_runner_)
    test_runner_->Teardown();
}

bool Reporter::connected() {
  return test_runner_.is_bound();
}
}  // namespace test_runner
