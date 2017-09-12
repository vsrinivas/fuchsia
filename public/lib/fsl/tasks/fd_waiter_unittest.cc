// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/fd_waiter.h"
#include "lib/fsl/tasks/message_loop.h"

namespace fsl {
namespace {

// Test disabled because it's hanging.
TEST(FDWaiter, DISABLED_WaitStdOut) {
  MessageLoop message_loop;

  FDWaiter waiter;
  EXPECT_TRUE(waiter.Wait(
      [&](mx_status_t status, uint32_t events) {
        EXPECT_EQ(MX_OK, status);
        EXPECT_TRUE(events & POLLOUT);
        message_loop.QuitNow();
      },
      STDOUT_FILENO, POLLOUT));

  message_loop.Run();
}

TEST(FDWaiter, WaitFailed) {
  MessageLoop message_loop;
  FDWaiter waiter;
  EXPECT_FALSE(
      waiter.Wait([](mx_status_t status, uint32_t events) {}, -1, POLLOUT));
}

}  // namespace
}  // namespace fsl
