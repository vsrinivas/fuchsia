// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(Pipe, PollInAndCloseWriteEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  struct pollfd polls[] = {{
      .fd = fds[0],
      .events = POLLIN,
      .revents = 0,
  }};

  EXPECT_EQ(0, poll(polls, 1, 1));

  close(fds[1]);

  EXPECT_EQ(1, poll(polls, 1, 1));

#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce POLLHUP on Fuchsia.
  EXPECT_EQ(POLLIN, polls[0].revents);
#else
  EXPECT_EQ(POLLHUP, polls[0].revents);
#endif
  close(fds[0]);
}

TEST(Pipe, PollOutEmptyPipeAndCloseReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  struct pollfd polls[] = {{
      .fd = fds[1],
      .events = POLLOUT,
      .revents = 0,
  }};

  EXPECT_EQ(1, poll(polls, 1, 1));

  EXPECT_EQ(POLLOUT, polls[0].revents);

  close(fds[0]);

#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce one event with POLLOUT | POLLERR on Fuchsia.
  EXPECT_EQ(0, poll(polls, 1, 1));
#else
  EXPECT_EQ(1, poll(polls, 1, 1));

  EXPECT_EQ(POLLOUT | POLLERR, polls[0].revents);
#endif
  close(fds[1]);
}

TEST(Pipe, PollOutFullPipeAndCloseReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));
  // TODO(https://fxbug.com/82011): Use pipe2 to set NONBLOCK when fdio implements it.
  ASSERT_SUCCESS(fcntl(fds[1], F_SETFL, O_NONBLOCK));

  // Fill pipe with data so POLLOUT is not asserted.
  constexpr size_t kBufSize = 4096;
  char buf[kBufSize] = {};

  while (true) {
    ssize_t rv = write(fds[1], buf, kBufSize);
    if (rv == -1) {
      ASSERT_EQ(errno, EWOULDBLOCK);
      break;
    }
    ASSERT_GT(rv, 0);
  }

  struct pollfd polls[] = {{
      .fd = fds[1],
      .events = POLLOUT,
      .revents = 0,
  }};

  EXPECT_EQ(0, poll(polls, 1, 1));

  close(fds[0]);

#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce one event with POLLERR on Fuchsia.
  EXPECT_EQ(0, poll(polls, 1, 1));
#else
  EXPECT_EQ(1, poll(polls, 1, 1));

  EXPECT_EQ(POLLERR, polls[0].revents);
#endif
  close(fds[1]);
}

// pipe2() is a Linux extension that Fuchsia supports.
#if defined(__Fuchsia__) || defined(__linux__)

TEST(Pipe2, NoFlags) {
  int fds[2];
  ASSERT_SUCCESS(pipe2(fds, 0));
  close(fds[0]);
  close(fds[1]);
}

TEST(Pipe2, Nonblock) {
  int fds[2];
  ASSERT_SUCCESS(pipe2(fds, O_NONBLOCK));
  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  int status_flags = fcntl(read_end.get(), F_GETFL);
  int fd_flags = fcntl(read_end.get(), F_GETFD);

  // Assert that the nonblock flag is set so we don't deadlock below.
  ASSERT_EQ(O_NONBLOCK, status_flags & O_NONBLOCK);

  // By default, close-on-exec is not set.
  EXPECT_NE(FD_CLOEXEC, fd_flags & FD_CLOEXEC);

  status_flags = fcntl(write_end.get(), F_GETFL);
  fd_flags = fcntl(write_end.get(), F_GETFD);

  ASSERT_EQ(O_NONBLOCK, status_flags & O_NONBLOCK);
  EXPECT_NE(FD_CLOEXEC, fd_flags & FD_CLOEXEC);

  constexpr size_t kBufSize = 4096;
  char buf[kBufSize] = {};

  // Reading from the read side of an empty pipe should return immediately.
  ssize_t rv = read(read_end.get(), buf, sizeof(buf));
  EXPECT_EQ(-1, rv);
  EXPECT_EQ(EWOULDBLOCK, errno);

  // Filling the write side of the pipe should eventually return EWOULDBLOCK.
  while (true) {
    ssize_t rv = write(write_end.get(), buf, kBufSize);
    if (rv == -1) {
      ASSERT_EQ(EWOULDBLOCK, errno);
      break;
    }
    ASSERT_GT(rv, 0);
  }
}

TEST(Pipe2, Cloexec) {
  int fds[2];
  ASSERT_SUCCESS(pipe2(fds, O_CLOEXEC));
  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  int status_flags = fcntl(read_end.get(), F_GETFL);
  int fd_flags = fcntl(read_end.get(), F_GETFD);

  EXPECT_NE(O_NONBLOCK, status_flags & O_NONBLOCK);
  EXPECT_EQ(FD_CLOEXEC, fd_flags & FD_CLOEXEC);

  status_flags = fcntl(write_end.get(), F_GETFL);
  fd_flags = fcntl(write_end.get(), F_GETFD);

  EXPECT_NE(O_NONBLOCK, status_flags & O_NONBLOCK);
  EXPECT_EQ(FD_CLOEXEC, fd_flags & FD_CLOEXEC);
}

#endif  // defined(__Fuchsia__) || defined(__linux__)
