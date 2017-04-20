// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "application/lib/app/application_context.h"
#include "apps/tracing/lib/trace/provider.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

// Thread connecting to the environment to allow tracing tests and reporting
// test results to test_runner.
class EnvironmentThread : public testing::EmptyTestEventListener {
 public:
  EnvironmentThread() {
    thread_.Run();
    thread_.TaskRunner()->PostTask([this] { InitOnThread(); });
  }
  ~EnvironmentThread() {
    thread_.TaskRunner()->PostTask([this] { QuitOnThread(); });
    thread_.Join();
  }

 private:
  void InitOnThread() {
    application_context_ =
        app::ApplicationContext::CreateFromStartupInfoNotChecked();
    if (application_context_->environment()) {
      tracing::InitializeTracer(application_context_.get(), {"ledger_tests"});
      InitTestReporting();
    }
  }

  void InitTestReporting() {
    test_runner_ = application_context_->ConnectToEnvironmentService<
        test_runner::TestRunner>();
    test_runner_->Identify("ledger_tests");
  }

  void QuitOnThread() { mtl::MessageLoop::GetCurrent()->PostQuitTask(); }

  // testing::EmptyTestEventListener override.
  // This gets called when all of the tests are done running
  void OnTestProgramEnd(const ::testing::UnitTest& test) override {
    if (!test_runner_) {
      return;
    }

    bool failed = test.Failed();
    thread_.TaskRunner()->PostTask([this, failed] {
      if (failed) {
        test_runner_->Fail("Failed");
      }
      test_runner_->Teardown();
    });
  }

  mtl::Thread thread_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  test_runner::TestRunnerPtr test_runner_;
};

int main(int argc, char** argv) {
  EnvironmentThread environment_thread;

  testing::InitGoogleTest(&argc, argv);
  testing::UnitTest::GetInstance()->listeners().Append(&environment_thread);
  int status = RUN_ALL_TESTS();
  testing::UnitTest::GetInstance()->listeners().Release(&environment_thread);
  return status;
}
