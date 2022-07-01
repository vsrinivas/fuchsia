// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {
TEST(PollTest, REventsIsCleared) {
  int pipefd[2];
  SAFE_SYSCALL(pipe2(pipefd, 0));

  struct pollfd fds[] = {{
                             .fd = pipefd[0],
                             .events = POLLIN,
                             .revents = 42,
                         },
                         {
                             .fd = pipefd[1],
                             .events = POLLOUT,
                             .revents = 42,
                         }};

  ASSERT_EQ(1, poll(fds, 2, 0));
  ASSERT_EQ(0, fds[0].revents);
  ASSERT_EQ(POLLOUT, fds[1].revents);
}
}  // namespace
