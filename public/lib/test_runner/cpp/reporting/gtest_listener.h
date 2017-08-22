// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_TEST_RUNNER_LIB_GTEST_LISTENER_H_
#define APPS_TEST_RUNNER_LIB_GTEST_LISTENER_H_

#include "apps/test_runner/services/test_runner.fidl.h"
#include "gtest/gtest.h"

namespace test_runner {

class Reporter;

// Handles events from the GoogleTest framework and publishes them to a
// Reporter.
class GTestListener : public ::testing::EmptyTestEventListener {
 public:
  GTestListener(const std::string& executable, Reporter* reporter);

  ~GTestListener() override;

  // testing::EmptyTestEventListener override.
  // Gets called when a single test (defined by a method) ends.
  void OnTestEnd(const ::testing::TestInfo& info) override;

  // testing::EmptyTestEventListener override.
  // Gets called when all of the tests are done running.
  void OnTestProgramEnd(const ::testing::UnitTest& test) override;

 private:
  std::string executable_;
  Reporter* reporter_;
};

}  // namespace test_runner

#endif  // APPS_TEST_RUNNER_LIB_GTEST_LISTENER_H_
