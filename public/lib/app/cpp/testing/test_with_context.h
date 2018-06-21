// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_
#define LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_

#include "lib/app/cpp/testing/startup_context_for_test.h"
#include "lib/gtest/test_with_loop.h"

namespace fuchsia {
namespace sys {
namespace testing {

// Test fixture for tests where a |StartupContext| is needed.
// Code under test can be given a context, while the test can use a |Controller|
// to set up and access the test environment.
class TestWithContext : public gtest::TestWithLoop {
  using Controller = StartupContextForTest::Controller;

 public:
  TestWithContext();
  void SetUp() override;
  void TearDown() override;

 protected:
  std::unique_ptr<StartupContext> TakeContext();
  const Controller& controller() const { return *controller_; }

 private:
  std::unique_ptr<StartupContextForTest> context_;
  const Controller* controller_;
};

}  // namespace testing
}  // namespace sys
}  // namespace fuchsia

#endif  // LIB_APP_CPP_TESTING_TEST_WITH_CONTEXT_H_
