// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <limits.h>
#include <sys/eventfd.h>
#include <sys/types.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(EventFDTest, Unsupported) {
  EXPECT_EQ(-1, eventfd(0, 39840));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
}

TEST(EventFDTest, Smoke) {
  fbl::unique_fd fd(eventfd(0, 0));
  EXPECT_TRUE(fd.is_valid());

  eventfd_t value = 7;
  EXPECT_EQ(0, eventfd_write(fd.get(), value));
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(7, value);

  value = 8;
  EXPECT_EQ(0, eventfd_write(fd.get(), value));
  value = 3;
  EXPECT_EQ(0, eventfd_write(fd.get(), value));
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(11, value);

  ASSERT_EQ(0, fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK));
  EXPECT_EQ(-1, eventfd_read(fd.get(), &value));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
}

TEST(EventFDTest, SmokeSemaphore) {
  fbl::unique_fd fd(eventfd(0, EFD_SEMAPHORE));
  EXPECT_TRUE(fd.is_valid());

  eventfd_t value = 7;
  EXPECT_EQ(0, eventfd_write(fd.get(), value));
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(1, value);
  // The event should now have a 6.

  value = 3;
  EXPECT_EQ(0, eventfd_write(fd.get(), value));
  // The event should now have a 9.

  for (size_t i = 0; i < 9; ++i) {
    value = 424;
    EXPECT_EQ(0, eventfd_read(fd.get(), &value));
    EXPECT_EQ(1, value);
  }

  // The event should now have a 0.
  ASSERT_EQ(0, fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK));
  EXPECT_EQ(-1, eventfd_read(fd.get(), &value));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
}

TEST(EventFDTest, InitialValue) {
  fbl::unique_fd fd(eventfd(343, 0));
  EXPECT_TRUE(fd.is_valid());

  eventfd_t value = 5464;
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(343, value);
}

TEST(EventFDTest, Cloexec) {
  fbl::unique_fd fd(eventfd(0, EFD_CLOEXEC));
  EXPECT_TRUE(fd.is_valid());

  int flags = fcntl(fd.get(), F_GETFL);
  EXPECT_FALSE(flags & FD_CLOEXEC);

  flags = fcntl(fd.get(), F_GETFD);
  EXPECT_TRUE(flags & FD_CLOEXEC);
}

TEST(EventFDTest, NonBlock) {
  fbl::unique_fd fd(eventfd(0, EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  int flags = fcntl(fd.get(), F_GETFL);
  EXPECT_TRUE(flags & O_NONBLOCK);
}

TEST(EventFDTest, WriteLimits) {
  fbl::unique_fd fd(eventfd(0, EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  EXPECT_EQ(-1, eventfd_write(fd.get(), UINT64_MAX));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  EXPECT_EQ(0, eventfd_write(fd.get(), UINT64_MAX - 5));
  EXPECT_EQ(0, eventfd_write(fd.get(), 3));
  EXPECT_EQ(-1, eventfd_write(fd.get(), 10));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
  EXPECT_EQ(-1, eventfd_write(fd.get(), 2));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
  EXPECT_EQ(0, eventfd_write(fd.get(), 1));

  eventfd_t value = 5464;
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(UINT64_MAX - 1, value);
}

static void check_signals(int fd, bool* out_is_readable, bool* out_is_writable) {
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd, &rfds);

  fd_set wfds;
  FD_ZERO(&wfds);
  FD_SET(fd, &wfds);

  struct timeval timeout = {};
  EXPECT_LT(0, select(fd + 1, &rfds, &wfds, nullptr, &timeout));

  *out_is_readable = FD_ISSET(fd, &rfds);
  *out_is_writable = FD_ISSET(fd, &wfds);
}

TEST(EventFDTest, Signals) {
  fbl::unique_fd fd(eventfd(0, EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  bool is_readable = false;
  bool is_writable = false;
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_FALSE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(0, eventfd_write(fd.get(), 75));
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(0, eventfd_write(fd.get(), UINT64_MAX - 76));
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_FALSE(is_writable);

  eventfd_t value = 5464;
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(UINT64_MAX - 1, value);
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_FALSE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(0, eventfd_write(fd.get(), 95));
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(-1, eventfd_write(fd.get(), UINT64_MAX));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(-1, eventfd_write(fd.get(), UINT64_MAX - 1));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
#ifdef __Fuchsia__
  // We get a different result than Linux here becaues we model blocking and
  // non-blocking I/O more uniformly. Linux appears to block the write that
  // would overflow while still having |select| report the eventfd as writable.
  // The way we set things up, |select| and |write| need to give consistent
  // views (or else a write that tries to block on an overflow would spin hot),
  // which means we have |select| report the eventfd as non-writable here.
  EXPECT_FALSE(is_writable);
#else
  EXPECT_TRUE(is_writable);
#endif

  value = 5464;
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(95, value);
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_FALSE(is_readable);
  EXPECT_TRUE(is_writable);
}

TEST(EventFDTest, SemaphoreSignals) {
  fbl::unique_fd fd(eventfd(0, EFD_SEMAPHORE | EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  EXPECT_EQ(0, eventfd_write(fd.get(), UINT64_MAX - 1));

  bool is_readable = false;
  bool is_writable = false;
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_FALSE(is_writable);

  eventfd_t value = 5464;
  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(1, value);

  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(1, value);

  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);

  EXPECT_EQ(-1, eventfd_write(fd.get(), 12));
  ASSERT_EQ(EAGAIN, errno, "errno incorrect");
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
#ifdef __Fuchsia__
  // We get a different result than Linux here becaues we model blocking and
  // non-blocking I/O more uniformly. Linux appears to block the write that
  // would overflow while still having |select| report the eventfd as writable.
  // The way we set things up, |select| and |write| need to give consistent
  // views (or else a write that tries to block on an overflow would spin hot),
  // which means we have |select| report the eventfd as non-writable here.
  EXPECT_FALSE(is_writable);
#else
  EXPECT_TRUE(is_writable);
#endif

  EXPECT_EQ(0, eventfd_read(fd.get(), &value));
  EXPECT_EQ(1, value);
  ASSERT_NO_FAILURES(check_signals(fd.get(), &is_readable, &is_writable));
  EXPECT_TRUE(is_readable);
  EXPECT_TRUE(is_writable);
}

TEST(EventFDTest, BufferLimits) {
  fbl::unique_fd fd(eventfd(42, EFD_SEMAPHORE | EFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  char buffer[64];
  memset(buffer, 0, sizeof(buffer));

  EXPECT_EQ(-1, read(fd.get(), buffer, 7));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  EXPECT_EQ(-1, write(fd.get(), buffer, 7));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
}
