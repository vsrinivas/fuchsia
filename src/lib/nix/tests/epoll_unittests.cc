// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/epoll.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

TEST(EpollTest, EpollCreateBad) {
  int epollfd = epoll_create(0);
  ASSERT_EQ(epollfd, -1, "epoll_create() did not fail with 0 arg");
}

TEST(EpollTest, EpollCreateClose) {
  int epollfd = epoll_create(1);
  ASSERT_GT(epollfd, -1, "epoll_create() failed");

  int closeres = close(epollfd);
  ASSERT_EQ(closeres, 0, "epoll close() failed");
}

TEST(EpollTest, EpollWait) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);
  epoll_event events[1];

  // Regular epoll_wait.
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 1, 1));

  close(epoll_fd);
}

TEST(EpollTest, EpollEventNoData) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Poll.
  epoll_event events[1];
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 1, 1));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEventData) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Ensure there's something in the pipe.
  ASSERT_EQ(1, write(fds[1], "\n", 1));

  // Poll.
  epoll_event events[1];
  events[0].events = 0;
  events[0].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 1, 1));
  ASSERT_EQ(expected, events[0].data.u64);

  char c;
  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEventDataMultiple) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Ensure there is something in the pipe.
  ASSERT_EQ(1, write(fds[1], "\n", 1));

  // Poll.
  epoll_event events[1];
  events[0].events = 0;
  events[0].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 1, 1));
  ASSERT_EQ(expected, events[0].data.u64);

  char c;
  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  // now there should be nothing to be read
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 1, 1));

  // now poll when there is something to be read
  ASSERT_EQ(1, write(fds[1], "\n", 1));
  events[0].events = 0;
  events[0].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 1, 1));
  ASSERT_EQ(expected, events[0].data.u64);

  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEventRemoveFd) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Ensure there is something in the pipe.
  ASSERT_EQ(1, write(fds[1], "\n", 1));

  // Now remove the fd
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fds[0], &ev));

  // Poll.
  epoll_event events[1];
  events[0].events = 0;
  events[0].data.u64 = 0;
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 1, 1));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEventWriteMod) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t unexpected = 0x1;
  const uint64_t expected = 0x2;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = unexpected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Ensure there is something in the pipe.
  ASSERT_EQ(1, write(fds[1], "\n", 1));

  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fds[0], &ev));

  // Poll.
  epoll_event events[2];
  events[0].events = 0;
  events[0].data.u64 = 0;
  events[1].events = 0;
  events[1].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));
  ASSERT_EQ(expected, events[0].data.u64);

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEventModWrite) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t unexpected = 0x1;
  const uint64_t expected = 0x2;

  // Get ready to poll on read end of pipe.
  epoll_event ev;
  ev.events = EPOLLIN;
  ev.data.u64 = unexpected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  ev.events = EPOLLIN;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fds[0], &ev));

  ASSERT_EQ(1, write(fds[1], "\n", 1));

  // Poll.
  epoll_event events[2];
  events[0].events = 0;
  events[0].data.u64 = 0;
  events[1].events = 0;
  events[1].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));
  ASSERT_EQ(expected, events[0].data.u64);

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}
