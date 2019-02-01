// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>

#include <lib/async-testutils/test_loop.h>

#include "gtest/gtest.h"
#include "lib/fsl/tasks/fd_waiter.h"

namespace fsl {
namespace {

// Test disabled because it's hanging.
TEST(FDWaiter, DISABLED_WaitStdOut) {
  async::TestLoop loop;

  FDWaiter waiter;
  EXPECT_TRUE(waiter.Wait(
      [&](zx_status_t status, uint32_t events) {
        EXPECT_EQ(ZX_OK, status);
        EXPECT_TRUE(events & POLLOUT);
        loop.Quit();
      },
      STDOUT_FILENO, POLLOUT));

  loop.RunUntilIdle();
}

TEST(FDWaiter, WaitFailed) {
  async::TestLoop loop;
  FDWaiter waiter;
  EXPECT_FALSE(
      waiter.Wait([](zx_status_t status, uint32_t events) {}, -1, POLLOUT));
}

}  // namespace
}  // namespace fsl
