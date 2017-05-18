// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_GTEST_REPORTER_H_
#define APPS_TEST_RUNNER_LIB_GTEST_REPORTER_H_

#include "application/lib/app/application_context.h"
#include "apps/test_runner/services/test_runner.fidl.h"
#include "apps/tracing/lib/trace/provider.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"
#include "lib/mtl/threading/thread.h"

namespace test_runner {

// Listens to test results from the gtest framework and reports them to the
// TestRunner FIDL service.
//
// Create an instance of this class after testing::InitGoogleTest() is called,
// and it will start listening and reporting.
class GoogleTestReporter : public testing::EmptyTestEventListener {
 public:
  // identity uniquely identifies this client to the TestRunner service.
  explicit GoogleTestReporter(const std::string& identity);
  ~GoogleTestReporter() override;

  void InitOnThread();

  void QuitOnThread();

  // testing::EmptyTestEventListener override.
  // This gets called when a single test (defined by a method) ends.
  void OnTestEnd(const ::testing::TestInfo& test) override;

  // testing::EmptyTestEventListener override.
  // This gets called when all of the tests are done running
  void OnTestProgramEnd(const ::testing::UnitTest& test) override;

 private:
  const std::string identity_;
  mtl::Thread thread_;
  std::unique_ptr<app::ApplicationContext> application_context_;
  TestRunnerPtr test_runner_;

  FTL_DISALLOW_COPY_AND_ASSIGN(GoogleTestReporter);
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_GTEST_REPORTER_H_
