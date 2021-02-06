// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/select.h>

#include <zxtest/zxtest.h>

namespace {

// Like with poll and ppoll, Fuchsia guarantees that selecting on 0
// fds in the fd_sets is equivalent to sleeping until the timeout.
//
// This is extremely similar to the tests of poll and ppoll in
// fdio_poll.cc.

TEST(Select, SelectZeroFds) {
  fd_set readfds;
  fd_set writefds;
  fd_set exceptfds;
  FD_ZERO(&readfds);
  FD_ZERO(&writefds);
  FD_ZERO(&exceptfds);

  constexpr std::chrono::duration minimum_duration = std::chrono::milliseconds(1);

  struct timeval timeout = {
      .tv_usec = std::chrono::microseconds(minimum_duration).count(),
  };
  const auto begin = std::chrono::steady_clock::now();
  ASSERT_EQ(select(0, &readfds, &writefds, &exceptfds, &timeout), 0, "%s", strerror(errno));
  ASSERT_GE(std::chrono::steady_clock::now() - begin, minimum_duration);

  // All bits in all the fd sets should be 0.
  for (int fd = 0; fd < FD_SETSIZE; ++fd) {
    ASSERT_TRUE(!FD_ISSET(fd, &readfds));
    ASSERT_TRUE(!FD_ISSET(fd, &writefds));
    ASSERT_TRUE(!FD_ISSET(fd, &exceptfds));
  }
}

}  // namespace
