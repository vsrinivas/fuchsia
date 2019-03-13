// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_SYS_CPP_TESTING_TEST_WITH_CONTEXT_H_
#define LIB_SYS_CPP_TESTING_TEST_WITH_CONTEXT_H_

#include <lib/sys/cpp/testing/component_context_for_test.h>
#include "lib/gtest/test_loop_fixture.h"

namespace sys {
namespace testing {

// Test fixture for tests where a |ComponentContext| is needed.
// Code under test can be given a context, while the test can use a |Controller|
// to set up and access the test environment.
class TestWithContext : public gtest::TestLoopFixture {
  using Controller = ComponentContextForTest::Controller;

 protected:
  TestWithContext();
  std::unique_ptr<ComponentContext> TakeContext();
  const Controller& controller() const { return *controller_; }

 private:
  std::unique_ptr<ComponentContextForTest> context_;
  const Controller* controller_;
};

}  // namespace testing
}  // namespace sys

#endif  // LIB_SYS_CPP_TESTING_TEST_WITH_CONTEXT_H_
