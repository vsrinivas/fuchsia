// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/gtest_reporter.h"

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"

namespace test_runner {

GoogleTestReporter::GoogleTestReporter(const std::string& identity) {
  app_context_ = app::ApplicationContext::CreateFromStartupInfoNotChecked();
  if (!app_context_->environment()) {
    // Allow the tests to run without reporting.
    return;
  }

  test_runner_ =
      app_context_->ConnectToEnvironmentService<test_runner::TestRunner>();
  test_runner_->Identify(identity);

  testing::UnitTest::GetInstance()->listeners().Append(this);
}

GoogleTestReporter::~GoogleTestReporter() {
  testing::UnitTest::GetInstance()->listeners().Release(this);
}

void GoogleTestReporter::OnTestProgramEnd(const testing::UnitTest& test) {
  if (test.Failed()) {
    test_runner_->Fail("Failed");
  }
  test_runner_->Teardown();
}

}
