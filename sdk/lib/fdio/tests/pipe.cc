// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

static constexpr int kPollTimeoutMs = 0;

TEST(Pipe, PollInAndCloseWriteEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  struct pollfd polls[] = {{
      .fd = fds[0],
      .events = POLLIN,
      .revents = 0,
  }};

  int n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(0, n);

  write_end.reset();

  n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(1, n);

#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce POLLHUP on Fuchsia.
  EXPECT_EQ(POLLIN, polls[0].revents);
#else
  EXPECT_EQ(POLLHUP, polls[0].revents);
#endif
}

TEST(Pipe, PollOutEmptyPipeAndCloseReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  struct pollfd polls[] = {{
      .fd = fds[1],
      .events = POLLOUT,
      .revents = 0,
  }};

  int n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(1, n);

  EXPECT_EQ(POLLOUT, polls[0].revents);

  read_end.reset();

  n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce one event with POLLOUT | POLLERR on Fuchsia.
  EXPECT_EQ(0, n);
#else
  EXPECT_EQ(1, n);

  EXPECT_EQ(POLLOUT | POLLERR, polls[0].revents);
#endif
}

TEST(Pipe, PollOutFullPipeAndCloseReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  ASSERT_SUCCESS(fcntl(write_end.get(), F_SETFL, O_NONBLOCK));

  // Fill pipe with data so POLLOUT is not asserted.
  constexpr size_t kBufSize = 4096;
  char buf[kBufSize] = {};

  while (true) {
    ssize_t rv = write(write_end.get(), buf, kBufSize);
    if (rv == -1) {
      ASSERT_EQ(errno, EWOULDBLOCK, "%s", strerror(errno));
      break;
    }
    ASSERT_GT(rv, 0);
  }

  struct pollfd polls[] = {{
      .fd = fds[1],
      .events = POLLOUT,
      .revents = 0,
  }};

  int n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(0, n);

  read_end.reset();

  n = poll(polls, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));
#ifdef __Fuchsia__
  // TODO(https://fxbug.dev/47132): This should produce one event with POLLERR on Fuchsia.
  EXPECT_EQ(0, n);
#else
  EXPECT_EQ(1, n);

  EXPECT_EQ(POLLERR, polls[0].revents);
#endif
}

TEST(Pipe, WriteIntoReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  ASSERT_SUCCESS(fcntl(read_end.get(), F_SETFL, O_NONBLOCK));

  constexpr char data = 'a';
  ssize_t rv = write(read_end.get(), &data, sizeof(data));
#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should fail on Fuchsia with EBADF.
  EXPECT_EQ(rv, 1, "%s", strerror(errno));
#else
  EXPECT_EQ(rv, -1);
  EXPECT_EQ(errno, EBADF, "%s", strerror(errno));
#endif  //  defined(__Fuchsia__)
}

TEST(Pipe, PollOutOnReadEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  ASSERT_SUCCESS(fcntl(read_end.get(), F_SETFL, O_NONBLOCK));

  struct pollfd read_end_pollout = {
      .fd = read_end.get(),
      .events = POLLOUT,
      .revents = 0,
  };

  int n = poll(&read_end_pollout, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should produce no events on Fuchsia.
  EXPECT_EQ(1, n);
  EXPECT_EQ(read_end_pollout.revents, POLLOUT);
  read_end_pollout.revents = 0;
#else
  EXPECT_EQ(0, n);
#endif  //  defined(__Fuchsia__)

  // Having data in the pipe should not change the POLLOUT behavior on the read end.
  constexpr char data = 'a';
  EXPECT_EQ(1, write(write_end.get(), &data, sizeof(data)), "%s", strerror(errno));

  n = poll(&read_end_pollout, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));
#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should produce no events on Fuchsia.
  EXPECT_EQ(1, n);
  EXPECT_EQ(read_end_pollout.revents, POLLOUT);
  read_end_pollout.revents = 0;
#else
  EXPECT_EQ(0, n);
#endif  //  defined(__Fuchsia__)

  // POLLOUT on the read end of a closed pipe should produce POLLHUP.
  write_end.reset();

  n = poll(&read_end_pollout, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));
#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should produce POLLHUP on Fuchsia.
  EXPECT_EQ(0, n);
#else
  EXPECT_EQ(1, n);
  EXPECT_EQ(read_end_pollout.revents, POLLHUP);
#endif  //  defined(__Fuchsia__)
}

TEST(Pipe, ReadFromWriteEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  ASSERT_SUCCESS(fcntl(write_end.get(), F_SETFL, O_NONBLOCK));

  char buf[1];
  ssize_t rv = read(write_end.get(), buf, sizeof(buf));
  EXPECT_EQ(rv, -1);
#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should fail on Fuchsia with EBADF.
  EXPECT_EQ(errno, EWOULDBLOCK, "%s", strerror(errno));
#else
  EXPECT_EQ(errno, EBADF, "%s", strerror(errno));
#endif  //  defined(__Fuchsia__)
}

TEST(Pipe, PollInOnWriteEnd) {
  int fds[2];
  ASSERT_SUCCESS(pipe(fds));

  fbl::unique_fd read_end(fds[0]);
  fbl::unique_fd write_end(fds[1]);

  ASSERT_SUCCESS(fcntl(read_end.get(), F_SETFL, O_NONBLOCK));

  struct pollfd write_end_pollin = {
      .fd = write_end.get(),
      .events = POLLIN,
      .revents = 0,
  };

  int n = poll(&write_end_pollin, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(0, n);

  // Having data in the pipe should not change the POLLIN behavior on the write end.
  constexpr char data = 'a';
  EXPECT_EQ(1, write(write_end.get(), &data, sizeof(data)), "%s", strerror(errno));

  n = poll(&write_end_pollin, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(0, n);

  // POLLIN on the write end of a closed pipe should produce POLLERR.
  read_end.reset();

  n = poll(&write_end_pollin, 1, kPollTimeoutMs);
  ASSERT_GE(n, 0, "%s", strerror(errno));

  EXPECT_EQ(1, n);
#if defined(__Fuchsia__)
  // TODO(https://fxbug.dev/84354): This should produce POLLHUP on Fuchsia.
  EXPECT_EQ(write_end_pollin.revents, POLLIN);
#else
  EXPECT_EQ(write_end_pollin.revents, POLLERR);
#endif  //  defined(__Fuchsia__)
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
  EXPECT_EQ(EWOULDBLOCK, errno, "%s", strerror(errno));

  // Filling the write side of the pipe should eventually return EWOULDBLOCK.
  while (true) {
    ssize_t rv = write(write_end.get(), buf, kBufSize);
    if (rv == -1) {
      ASSERT_EQ(EWOULDBLOCK, errno, "%s", strerror(errno));
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
