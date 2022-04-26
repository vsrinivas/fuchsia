// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <signal.h>
#include <unistd.h>

#include <gtest/gtest.h>

static int rfd = -1;
static int wfd = -1;

static volatile pid_t child_pid = -1;
static volatile bool child_wrote_data = false;
static volatile bool parent_read_started = false;

static void sig_handler(int, siginfo_t*, void*) {
  if (!parent_read_started || child_pid <= 0) {
    // The parent has not entered the read syscall or the child PID as not been retrieved yet:
    // ignore.
    return;
  }

  // The parent is reading now, tell the child to send the requested payload.
  kill(child_pid, SIGUSR2);
}

static void sig_handler_child(int, siginfo_t* siginfo, void*) {
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

  pid_t pid = fork();
  ASSERT_NE(pid, -1);
  if (pid == 0) {
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

    // Explicitly exit so that we don't run the gtest reporting in the child as well.
    _exit(EXIT_SUCCESS);
  } else {
    // Parent process.

    // Let the signal handler know which child to send a signal to.
    child_pid = pid;

    // Read the expected payload. The syscall will be interrupted, but userspace shouldn't be
    // aware of this (as in, the result should NOT be EINTR).
    int expected_data = 0;
    parent_read_started = true;
    ASSERT_EQ(read(rfd, &expected_data, sizeof(expected_data)), (ssize_t)sizeof(expected_data));
    EXPECT_EQ(expected_data, 1);

    close(rfd);
    close(wfd);

    // Wait for the child to exit cleanly.
    int child_status = 0;
    ASSERT_EQ(wait(&child_status), child_pid);
    ASSERT_EQ(child_status, 0);
  }
}
