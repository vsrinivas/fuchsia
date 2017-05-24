// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_

#include "application/lib/app/application_context.h"
#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

// Run message loop until condition is false (timeout after 400*10ms = 4000ms)
#define RUN_MESSAGE_LOOP_WHILE(condition)                       \
  {                                                             \
    for (int i = 0; condition && i < 400; i++) {                \
      RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(10)); \
    }                                                           \
  }

namespace mozart {
namespace test {

// Starts a message loop and runs tests. If TestRunner exists, calls the
// appropriate methods on setup and teardown. Used in main() by unit tests.
//
// |run_tests| is the function that runs the tests. Accepts the application
// context as a parameter. It returns the status code after running tests
// (i.e. 0 if success).
//
// |tests_name| identifies the tests in TestRunner.
//
// Returns the status code returned by |run_tests|.
int RunTestsWithMessageLoopAndTestRunner(
    std::string tests_name,
    std::function<int(app::ApplicationContext*)> run_tests);

class TestWithMessageLoop : public ::testing::Test {
 public:
  TestWithMessageLoop() {}

  void SetUp() override {
    FTL_CHECK(nullptr != mtl::MessageLoop::GetCurrent());
  }

 protected:
  // Run the loop for at most |timeout|. Returns |true| if the timeout has
  // been
  // reached.
  bool RunLoopWithTimeout(
      ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TestWithMessageLoop);
};

}  // namespace test
}  // namespace mozart

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_
