// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ledger/bin/environment/test_loop_notification.h"

#include <lib/async/cpp/task.h>

#include "gtest/gtest.h"
#include "src/ledger/lib/loop_fixture/test_loop_fixture.h"

namespace ledger {
namespace {

using TestLoopNotificationTest = TestLoopFixture;

TEST_F(TestLoopNotificationTest, NotifyAcrossSubloops) {
  TestLoopNotification notification(&test_loop());
  auto subloop = test_loop().StartNewLoop();
  bool wait_returned = false;
  bool subloop_called = false;
  async::PostTask(subloop->dispatcher(), [&] {
    EXPECT_FALSE(wait_returned);
    subloop_called = true;
    notification.Notify();
  });
  async::PostTask(dispatcher(), [&] {
    notification.WaitForNotification();
    EXPECT_TRUE(subloop_called);
    wait_returned = true;
  });

  RunLoopUntilIdle();
  EXPECT_TRUE(subloop_called);
  EXPECT_TRUE(wait_returned);
}

}  // namespace
}  // namespace ledger
