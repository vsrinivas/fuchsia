// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/test_with_message_loop.h"

namespace test {
namespace {

class TestWithMessageLoopTest : public TestWithMessageLoop {};

TEST_F(TestWithMessageLoopTest, TestTimeout) {
  bool called = false;
  message_loop_.task_runner()->PostDelayedTask([&called] { called = true; },
                                               fxl::TimeDelta::FromSeconds(1));
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(10)));
}

TEST_F(TestWithMessageLoopTest, TestNoTimeout) {
  message_loop_.PostQuitTask();

  // Check that the first run loop doesn't hit the timeout.
  EXPECT_FALSE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(10)));

  // But the second does.
  EXPECT_TRUE(RunLoopWithTimeout(fxl::TimeDelta::FromMilliseconds(20)));
}

}  // namespace
}  // namespace test
