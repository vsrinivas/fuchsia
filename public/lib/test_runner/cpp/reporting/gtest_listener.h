// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_TEST_RUNNER_CPP_REPORTING_GTEST_LISTENER_H_
#define LIB_TEST_RUNNER_CPP_REPORTING_GTEST_LISTENER_H_

#include "lib/test_runner/fidl/test_runner.fidl.h"
#include "gtest/gtest.h"

namespace test_runner {

// Handles events from the GoogleTest framework and stores them.
class GTestListener : public ::testing::EmptyTestEventListener {
 public:
  GTestListener(const std::string& executable);

  ~GTestListener() override;

  // testing::EmptyTestEventListener override.
  // Gets called when a single test (defined by a method) ends.
  void OnTestEnd(const ::testing::TestInfo& info) override;

  // testing::EmptyTestEventListener override.
  // Gets called when all of the tests are done running.
  void OnTestProgramEnd(const ::testing::UnitTest& test) override;

  std::vector<TestResultPtr> GetResults();

 private:
  std::string executable_;
  std::vector<TestResultPtr> results_;
};

}  // namespace test_runner

#endif  // LIB_TEST_RUNNER_CPP_REPORTING_GTEST_LISTENER_H_
