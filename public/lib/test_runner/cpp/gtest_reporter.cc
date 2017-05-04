// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/test_runner/lib/gtest_reporter.h"

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"

namespace test_runner {

GoogleTestReporter::GoogleTestReporter(const std::string& identity)
    : identity_(identity) {
  thread_.Run();
  thread_.TaskRunner()->PostTask([this] { InitOnThread(); });
}

GoogleTestReporter::~GoogleTestReporter() {
  thread_.TaskRunner()->PostTask([this] { QuitOnThread(); });
  thread_.Join();
}

void GoogleTestReporter::InitOnThread() {
  application_context_ =
      app::ApplicationContext::CreateFromStartupInfoNotChecked();
  if (application_context_->environment()) {
    tracing::InitializeTracer(application_context_.get(), {identity_});
    test_runner_ = application_context_
                       ->ConnectToEnvironmentService<test_runner::TestRunner>();
    test_runner_->Identify(identity_);
  }
}

void GoogleTestReporter::QuitOnThread() {
  mtl::MessageLoop::GetCurrent()->PostQuitTask();
}

void GoogleTestReporter::OnTestProgramEnd(const ::testing::UnitTest& test) {
  bool failed = test.Failed();
  thread_.TaskRunner()->PostTask([this, failed] {
    if (!test_runner_) {
      return;
    }
    if (failed) {
      test_runner_->Fail("Failed");
    }
    test_runner_->Teardown();
  });
}

}  // namespace test_runner
