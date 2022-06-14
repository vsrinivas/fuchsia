// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include <gtest/gtest.h>

TEST(UnixSocket, HupEvent) {
  int fds[2];

  ASSERT_EQ(0, socketpair(AF_UNIX, SOCK_STREAM, 0, fds));

  int epfd = epoll_create1(0);
  ASSERT_LT(-1, epfd);
  epoll_event ev = {EPOLLIN, {.u64 = 42}};
  ASSERT_EQ(0, epoll_ctl(epfd, EPOLL_CTL_ADD, fds[0], &ev));

  epoll_event outev = {0, {.u64 = 0}};

  int no_ready = epoll_wait(epfd, &outev, 1, 0);
  ASSERT_EQ(0, no_ready);

  close(fds[1]);

  no_ready = epoll_wait(epfd, &outev, 1, 0);
  ASSERT_EQ(1, no_ready);
  ASSERT_EQ(EPOLLIN | EPOLLHUP, outev.events);
  ASSERT_EQ(42ul, outev.data.u64);

  close(fds[0]);
  close(epfd);
}
