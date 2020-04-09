// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <zircon/time.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#ifdef __Fuchsia__

#include <zircon/syscalls.h>

#else

static zx_time_t zx_clock_get_monotonic() {
  struct timespec t = {};
  clock_gettime(CLOCK_MONOTONIC, &t);
  return ZX_SEC(t.tv_sec) + t.tv_nsec;
}

#endif

TEST(TimerFDTest, Unsupported) {
#ifdef __Fuchsia__
  EXPECT_EQ(-1, timerfd_create(CLOCK_REALTIME, 0));
  ASSERT_EQ(ENOSYS, errno, "errno incorrect");
#endif
  EXPECT_EQ(-1, timerfd_create(2513, 0));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
  EXPECT_EQ(-1, timerfd_create(CLOCK_MONOTONIC, 4512));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
}

TEST(TimerFDTest, Cloexec) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC));
  EXPECT_TRUE(fd.is_valid());

  int flags = fcntl(fd.get(), F_GETFL);
  EXPECT_FALSE(flags & FD_CLOEXEC);

  flags = fcntl(fd.get(), F_GETFD);
  EXPECT_TRUE(flags & FD_CLOEXEC);
}

TEST(TimerFDTest, NonBlock) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK));
  EXPECT_TRUE(fd.is_valid());

  int flags = fcntl(fd.get(), F_GETFL);
  EXPECT_TRUE(flags & O_NONBLOCK);
}

TEST(TimerFDTest, OneShotLifecycle) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());

  struct itimerspec value = {};
  EXPECT_EQ(-1, timerfd_gettime(23984, &value));
  ASSERT_EQ(EBADF, errno, "errno incorrect");
  EXPECT_EQ(-1, timerfd_gettime(STDOUT_FILENO, &value));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  memset(&value, 'a', sizeof(value));
  EXPECT_EQ(0, timerfd_gettime(fd.get(), &value));
  EXPECT_EQ(0, value.it_value.tv_sec);
  EXPECT_EQ(0, value.it_value.tv_nsec);
  EXPECT_EQ(0, value.it_interval.tv_sec);
  EXPECT_EQ(0, value.it_interval.tv_nsec);

  value.it_value.tv_nsec = ZX_MSEC(5);
  EXPECT_EQ(-1, timerfd_settime(23984, 0, &value, nullptr));
  ASSERT_EQ(EBADF, errno, "errno incorrect");
  EXPECT_EQ(-1, timerfd_settime(STDOUT_FILENO, 0, &value, nullptr));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");
  EXPECT_EQ(-1, timerfd_settime(fd.get(), 4512, &value, nullptr));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  value.it_value.tv_nsec = -ZX_MSEC(5);
  EXPECT_EQ(-1, timerfd_settime(fd.get(), 0, &value, nullptr));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  value.it_value.tv_nsec = ZX_MSEC(5);
  value.it_interval.tv_nsec = -ZX_MSEC(5);
  EXPECT_EQ(-1, timerfd_settime(fd.get(), 0, &value, nullptr));
  ASSERT_EQ(EINVAL, errno, "errno incorrect");

  zx_time_t start = zx_clock_get_monotonic();
  value.it_value.tv_nsec = ZX_MSEC(5);
  value.it_interval.tv_nsec = 0;
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t counter = 2934;
  EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
  zx_time_t end = zx_clock_get_monotonic();

  EXPECT_EQ(1, counter);
  EXPECT_LE(ZX_MSEC(5), end - start);

  EXPECT_EQ(0, timerfd_gettime(fd.get(), &value));
  EXPECT_EQ(0, value.it_value.tv_sec);
  EXPECT_EQ(0, value.it_value.tv_nsec);
  EXPECT_EQ(0, value.it_interval.tv_sec);
  EXPECT_EQ(0, value.it_interval.tv_nsec);

  ASSERT_EQ(0, fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK));
  EXPECT_EQ(-1, read(fd.get(), &counter, sizeof(counter)));
  ASSERT_EQ(EWOULDBLOCK, errno, "errno incorrect");
}

TEST(TimerFDTest, OneShotNonblocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());
  ASSERT_EQ(0, fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK));

  uint64_t counter = 2934;
  EXPECT_EQ(-1, read(fd.get(), &counter, sizeof(counter)));
  ASSERT_EQ(EWOULDBLOCK, errno, "errno incorrect");

  struct itimerspec value = {};
  value.it_value.tv_sec = 600;
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, nullptr));

  EXPECT_EQ(-1, read(fd.get(), &counter, sizeof(counter)));
  ASSERT_EQ(EWOULDBLOCK, errno, "errno incorrect");

  struct itimerspec old_value = {};
  value.it_value.tv_sec = 0;
  value.it_value.tv_nsec = ZX_MSEC(5);
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, &old_value));
  EXPECT_GE(600, old_value.it_value.tv_sec);

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd.get(), &rfds);

  EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));
  EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
  EXPECT_EQ(1, counter);

  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, &old_value));
  EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));
  EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
  EXPECT_EQ(1, counter);
}

TEST(TimerFDTest, RepeatingBlocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());

  zx_time_t start = zx_clock_get_monotonic();

  struct itimerspec value = {};
  value.it_value.tv_nsec = ZX_MSEC(15);
  value.it_interval.tv_nsec = ZX_MSEC(15);
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t total = 0u;
  for (size_t i = 0; i < 5; ++i) {
    uint64_t counter = 2934;
    EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
    EXPECT_LE(1, counter);
    total += counter;
  }

  zx_time_t end = zx_clock_get_monotonic();
  EXPECT_LE(total * ZX_MSEC(15), end - start);
}

TEST(TimerFDTest, Counter) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());

  zx_time_t start = zx_clock_get_monotonic();

  struct itimerspec value = {};
  value.it_value.tv_nsec = ZX_MSEC(5);
  value.it_interval.tv_nsec = ZX_MSEC(5);
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, nullptr));
  uint64_t counter = 2934;
  EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
  EXPECT_LE(1, counter);

  struct timespec sleep = {.tv_sec = 0, .tv_nsec = ZX_MSEC(20)};
  EXPECT_EQ(0, nanosleep(&sleep, nullptr));

  EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
  EXPECT_LE(4, counter);

  zx_time_t end = zx_clock_get_monotonic();
  EXPECT_LE(5 * ZX_MSEC(5), end - start);
}

TEST(TimerFDTest, RepeatingNonblocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());
  ASSERT_EQ(0, fcntl(fd.get(), F_SETFL, fcntl(fd.get(), F_GETFL) | O_NONBLOCK));

  zx_time_t start = zx_clock_get_monotonic();

  struct itimerspec value = {};
  value.it_value.tv_nsec = ZX_MSEC(15);
  value.it_interval.tv_nsec = ZX_MSEC(15);
  EXPECT_EQ(0, timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t total = 0u;
  for (size_t i = 0; i < 5; ++i) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd.get(), &rfds);

    EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));

    uint64_t counter = 2934;
    EXPECT_EQ(sizeof(counter), read(fd.get(), &counter, sizeof(counter)));
    EXPECT_LE(1, counter);
    total += counter;
  }

  zx_time_t end = zx_clock_get_monotonic();
  EXPECT_LE(total * ZX_MSEC(15), end - start);
}
