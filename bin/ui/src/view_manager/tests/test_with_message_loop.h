// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_
#define APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_

#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

// Run message loop until condition is true (timeout after 400*10ms = 4000ms)
#define RUN_MESSAGE_LOOP_WHILE(x)                               \
  {                                                             \
    for (int i = 0; x && i < 400; i++) {                        \
      RunLoopWithTimeout(ftl::TimeDelta::FromMilliseconds(10)); \
    }                                                           \
  }

namespace view_manager {
namespace test {

class TestWithMessageLoop : public ::testing::Test {
 public:
  TestWithMessageLoop() {}

 protected:
  // Run the loop for at most |timeout|. Returns |true| if the timeout has
  // been
  // reached.
  bool RunLoopWithTimeout(
      ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1));

  mtl::MessageLoop message_loop_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TestWithMessageLoop);
};

}  // namespace test
}  // namespace view_manager

#endif  // APPS_MOZART_SRC_VIEW_MANAGER_TESTS_TEST_WITH_MESSAGE_LOOP_H_
