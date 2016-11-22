// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_LEDGER_SRC_TEST_TEST_WITH_MESSAGE_LOOP_H_
#define APPS_LEDGER_SRC_TEST_TEST_WITH_MESSAGE_LOOP_H_

#include "gtest/gtest.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/mtl/tasks/message_loop.h"

namespace test {

class TestWithMessageLoop : public ::testing::Test {
 public:
  TestWithMessageLoop() {}

 protected:
  void RunLoopWithTimeout(
      ftl::TimeDelta timeout = ftl::TimeDelta::FromSeconds(1)) {
    message_loop_.task_runner()->PostDelayedTask(
        [this] {
          message_loop_.PostQuitTask();
          FAIL();
        },
        timeout);
    message_loop_.Run();
  }

  mtl::MessageLoop message_loop_;

 private:
  FTL_DISALLOW_COPY_AND_ASSIGN(TestWithMessageLoop);
};

}  // namespace test

#endif  // APPS_LEDGER_SRC_TEST_TEST_WITH_MESSAGE_LOOP_H_
