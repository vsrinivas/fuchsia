// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/callback/asynchronous_callback.h"
#include "apps/ledger/src/test/test_with_message_loop.h"
#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

namespace callback {
namespace {

class AsynchronousCallbackTest : public test::TestWithMessageLoop {};

TEST_F(AsynchronousCallbackTest, RunAsynchronously) {
  bool called = false;
  std::unique_ptr<int> value;
  MakeAsynchronous(
      [this, &called, &value](std::unique_ptr<int> new_value) {
        called = true;
        value = std::move(new_value);
        message_loop_.QuitNow();
      },
      message_loop_.task_runner())(std::make_unique<int>(0));
  EXPECT_FALSE(called);
  EXPECT_FALSE(value);
  EXPECT_FALSE(RunLoopWithTimeout());
  EXPECT_TRUE(called);
  ASSERT_TRUE(value);
  EXPECT_EQ(0, *value);
}

}  // namespace
}  // namespace callback
