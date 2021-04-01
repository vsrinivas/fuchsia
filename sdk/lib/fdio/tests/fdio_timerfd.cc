// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

#include "predicates.h"

TEST(TimerFDTest, Unsupported) {
#ifdef __Fuchsia__
  EXPECT_EQ(timerfd_create(CLOCK_REALTIME, 0), -1);
  ASSERT_ERRNO(ENOSYS);
#endif
  EXPECT_EQ(timerfd_create(2513, 0), -1);
  ASSERT_ERRNO(EINVAL);
  EXPECT_EQ(timerfd_create(CLOCK_MONOTONIC, 4512), -1);
  ASSERT_ERRNO(EINVAL);
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
  EXPECT_EQ(timerfd_gettime(23984, &value), -1);
  ASSERT_ERRNO(EBADF);
  EXPECT_EQ(timerfd_gettime(STDOUT_FILENO, &value), -1);
  ASSERT_ERRNO(EINVAL);

  memset(&value, 'a', sizeof(value));
  EXPECT_EQ(timerfd_gettime(fd.get(), &value), 0);
  EXPECT_EQ(value.it_value.tv_sec, 0);
  EXPECT_EQ(value.it_value.tv_nsec, 0);
  EXPECT_EQ(value.it_interval.tv_sec, 0);
  EXPECT_EQ(value.it_interval.tv_nsec, 0);

  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  EXPECT_EQ(timerfd_settime(23984, 0, &value, nullptr), -1);
  ASSERT_ERRNO(EBADF);
  EXPECT_EQ(timerfd_settime(STDOUT_FILENO, 0, &value, nullptr), -1);
  ASSERT_ERRNO(EINVAL);
  EXPECT_EQ(timerfd_settime(fd.get(), 4512, &value, nullptr), -1);
  ASSERT_ERRNO(EINVAL);

  value.it_value.tv_nsec = -std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  EXPECT_EQ(timerfd_settime(fd.get(), 0, &value, nullptr), -1);
  ASSERT_ERRNO(EINVAL);

  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  value.it_interval.tv_nsec = -std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  EXPECT_EQ(timerfd_settime(fd.get(), 0, &value, nullptr), -1);
  ASSERT_ERRNO(EINVAL);

  const auto start = std::chrono::steady_clock::now();
  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  value.it_interval.tv_nsec = 0;
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t counter = 2934;
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
  const auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(counter, 1u);
  EXPECT_GE(end - start, std::chrono::milliseconds(5));

  EXPECT_EQ(timerfd_gettime(fd.get(), &value), 0);
  EXPECT_EQ(value.it_value.tv_sec, 0);
  EXPECT_EQ(value.it_value.tv_nsec, 0);
  EXPECT_EQ(value.it_interval.tv_sec, 0);
  EXPECT_EQ(value.it_interval.tv_nsec, 0);

  int flags;
  EXPECT_GE(flags = fcntl(fd.get(), F_GETFL), 0, "%s", strerror(errno));
  EXPECT_SUCCESS(fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK));
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), -1);
  ASSERT_ERRNO(EWOULDBLOCK);
}

TEST(TimerFDTest, OneShotNonblocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());
  int flags;
  EXPECT_GE(flags = fcntl(fd.get(), F_GETFL), 0, "%s", strerror(errno));
  EXPECT_SUCCESS(fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK));

  uint64_t counter = 2934;
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), -1);
  ASSERT_ERRNO(EWOULDBLOCK);

  constexpr time_t set_sec = 600;

  struct itimerspec value = {};
  value.it_value.tv_sec = set_sec;
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, nullptr));

  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), -1);
  ASSERT_ERRNO(EWOULDBLOCK);

  struct itimerspec old_value = {};
  value.it_value.tv_sec = 0;
  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, &old_value));
  EXPECT_LE(old_value.it_value.tv_sec, set_sec);

  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(fd.get(), &rfds);

  EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
  EXPECT_EQ(counter, 1u);

  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, &old_value));
  EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)), "%s",
            strerror(errno));
  EXPECT_EQ(counter, 1u);
}

TEST(TimerFDTest, RepeatingBlocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());

  const auto start = std::chrono::steady_clock::now();

  struct itimerspec value = {};
  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(15)).count();
  value.it_interval.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(15)).count();
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t total = 0u;
  for (size_t i = 0; i < 5; ++i) {
    uint64_t counter = 2934;
    EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
    EXPECT_GE(counter, 1u);
    total += counter;
  }

  const auto end = std::chrono::steady_clock::now();
  EXPECT_GE(end - start, total * std::chrono::milliseconds(15));
}

TEST(TimerFDTest, Counter) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());

  const auto start = std::chrono::steady_clock::now();

  struct itimerspec value = {};
  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  value.it_interval.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(5)).count();
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, nullptr));
  uint64_t counter = 2934;
  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
  EXPECT_GE(counter, 1u);

  struct timespec sleep = {
      .tv_sec = 0, .tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(20)).count()};
  EXPECT_EQ(nanosleep(&sleep, nullptr), 0);

  EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
  EXPECT_GE(counter, 4u);

  const auto end = std::chrono::steady_clock::now();
  EXPECT_GE(end - start, 5 * std::chrono::milliseconds(5));
}

TEST(TimerFDTest, RepeatingNonblocking) {
  fbl::unique_fd fd(timerfd_create(CLOCK_MONOTONIC, 0));
  EXPECT_TRUE(fd.is_valid());
  int flags;
  EXPECT_GE(flags = fcntl(fd.get(), F_GETFL), 0, "%s", strerror(errno));
  EXPECT_SUCCESS(fcntl(fd.get(), F_SETFL, flags | O_NONBLOCK));

  const auto start = std::chrono::steady_clock::now();

  struct itimerspec value = {};
  value.it_value.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(15)).count();
  value.it_interval.tv_nsec = std::chrono::nanoseconds(std::chrono::milliseconds(15)).count();
  EXPECT_SUCCESS(timerfd_settime(fd.get(), 0, &value, nullptr));

  uint64_t total = 0u;
  for (size_t i = 0; i < 5; ++i) {
    fd_set rfds;
    FD_ZERO(&rfds);
    FD_SET(fd.get(), &rfds);

    EXPECT_LT(0, select(fd.get() + 1, &rfds, nullptr, nullptr, nullptr));

    uint64_t counter = 2934;
    EXPECT_EQ(read(fd.get(), &counter, sizeof(counter)), ssize_t(sizeof(counter)));
    EXPECT_GE(counter, 1u);
    total += counter;
  }

  const auto end = std::chrono::steady_clock::now();
  EXPECT_GE(end - start, total * std::chrono::milliseconds(15));
}
