// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <unistd.h>

#include <gtest/gtest.h>

#include "src/proc/tests/chromiumos/syscalls/test_helper.h"

namespace {

int rfd = -1;
int wfd = -1;

volatile pid_t child_pid = -1;
volatile bool child_wrote_data = false;
volatile bool parent_read_started = false;

void sig_handler(int, siginfo_t*, void*) {
  if (!parent_read_started || child_pid <= 0) {
    // The parent has not entered the read syscall or the child PID as not been retrieved yet:
    // ignore.
    return;
  }

  // The parent is reading now, tell the child to send the requested payload.
  kill(child_pid, SIGUSR2);
}

void sig_handler_child(int, siginfo_t* siginfo, void*) {
  if (child_wrote_data) {
    // The child already wrote the data: ignore.
    return;
  }

  // This will cause the child to stop sending `kill` and exit.
  child_wrote_data = true;

  // Send the expected payload to the parent.
  const int data = 1;
  write(wfd, &data, sizeof(data));
}

TEST(RestartRead, ReadFromPipeRestarts) {
  // Reset global state to allow test repetition.
  rfd = -1;
  wfd = -1;
  child_pid = -1;
  child_wrote_data = false;
  parent_read_started = false;

  ForkHelper helper;
  // Install the signal handler that will interrupt the read syscall. The `SA_RESTART` flag tells
  // the kernel to restart any interrupted syscalls that support being restarted.
  struct sigaction sa = {};
  sa.sa_sigaction = sig_handler;
  sa.sa_flags = SA_SIGINFO | SA_RESTART;
  ASSERT_EQ(sigaction(SIGUSR1, &sa, NULL), 0);

  // Create the pipe that will be used to communicate with the child process.
  int pipefd[2];
  ASSERT_EQ(pipe(pipefd), 0);
  rfd = pipefd[0];
  wfd = pipefd[1];

  child_pid = helper.RunInForkedProcess([&] {
    // Child process.

    // Install a signal handler that will write the expected payload to the parent when signaled by
    // the parent.
    struct sigaction sa = {};
    sa.sa_sigaction = sig_handler_child;
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    ASSERT_EQ(sigaction(SIGUSR2, &sa, NULL), 0);

    // Send a series of signals to the parent process, which should continue to interrupt the
    // parent's read syscall until the payload is written.
    while (!child_wrote_data) {
      kill(getppid(), SIGUSR1);
    }

    close(rfd);
    close(wfd);
  });

  // Parent process.

  // Read the expected payload. The syscall will be interrupted, but userspace shouldn't be
  // aware of this (as in, the result should NOT be EINTR).
  int expected_data = 0;
  parent_read_started = true;
  EXPECT_EQ(read(rfd, &expected_data, sizeof(expected_data)), (ssize_t)sizeof(expected_data));
  EXPECT_EQ(expected_data, 1);

  close(rfd);
  close(wfd);
}
}  // namespace
