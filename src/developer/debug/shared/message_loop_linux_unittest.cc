// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/debug/shared/message_loop_linux.h"

#include <unistd.h>

#include <gtest/gtest.h>

namespace debug {

// Tests that we can get SIGCHLD signals.
TEST(MessageLoopLinux, SigChild) {
  MessageLoopLinux loop;
  std::string error_message;
  ASSERT_TRUE(loop.Init(&error_message)) << error_message;

  // Scope everything to before MessageLoop::Cleanup().
  {
    // For and then exit from the child, this will just send the SIGCHLD to the test.
    int child_pid = fork();
    if (child_pid == 0) {
      exit(1);
    }

    std::optional<int> result_status;
    MessageLoop::WatchHandle watch_handle = loop.WatchChildSignals(
        child_pid, [child_pid, &result_status, &loop](pid_t pid, int status) {
          EXPECT_EQ(child_pid, pid);
          result_status = status;
          loop.QuitNow();
        });

    loop.Run();

    // Callback should have run.
    ASSERT_TRUE(result_status);

    // The message should have been that the process exited.
    EXPECT_TRUE(WIFEXITED(*result_status));
  }

  loop.Cleanup();
}

}  // namespace debug
