// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/mtl/threading/thread.h"

#include "gtest/gtest.h"
#include "lib/mtl/tasks/message_loop.h"

namespace mtl {
namespace {

TEST(Thread, Control) {
  Thread thread;
  EXPECT_FALSE(thread.IsRunning());
  EXPECT_TRUE(thread.Run());
  EXPECT_TRUE(thread.IsRunning());
  thread.TaskRunner()->PostTask(
      [] { mtl::MessageLoop::GetCurrent()->QuitNow(); });
  EXPECT_TRUE(thread.Join());
}

}  // namespace
}  // namespace mtl
