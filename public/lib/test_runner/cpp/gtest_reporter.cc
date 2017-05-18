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
  thread_.TaskRunner()->PostTask([this] {
    // If we have a test runner, then the Teardown callback will quit this
    // thread. Otherwise, we have to do it here.
    if (!test_runner_) {
      QuitOnThread();
    }
  });
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

void GoogleTestReporter::OnTestEnd(const ::testing::TestInfo& info) {
  auto gtest_result = info.result();

  std::string name(info.test_case_name());
  name += ".";
  name += info.name();

  auto elapsed = gtest_result->elapsed_time();
  bool failed = gtest_result->Failed();

  std::stringstream stream;
  int part_count = gtest_result->total_part_count();
  for (int i = 0; i < part_count; i++) {
    auto part_result = gtest_result->GetTestPartResult(i);
    if (part_result.failed()) {
      stream << part_result.file_name() << ":"
        << part_result.line_number() << "\n"
        << part_result.message() << "\n";
    }
  }
  std::string message = stream.str();

  thread_.TaskRunner()->PostTask([this, name, elapsed, failed, message] {
    if (test_runner_) {
      TestResultPtr result = TestResult::New();
      result->name = name;
      result->elapsed = elapsed;
      result->failed = failed;
      result->message = message;
      test_runner_->ReportResult(std::move(result));
    }
  });
}

void GoogleTestReporter::OnTestProgramEnd(const ::testing::UnitTest& test) {
  thread_.TaskRunner()->PostTask([this] {
    if (!test_runner_) {
      return;
    }
    test_runner_->Teardown([this] {
      test_runner_.reset();
      QuitOnThread();
    });
  });
}

}  // namespace test_runner
