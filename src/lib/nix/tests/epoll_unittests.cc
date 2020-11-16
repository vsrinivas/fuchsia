// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/epoll.h>
#include <sys/eventfd.h>
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
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

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
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

  char c;
  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  // now there should be nothing to be read
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 1, 1));

  // now poll when there is something to be read
  ASSERT_EQ(1, write(fds[1], "\n", 1));
  events[0].events = 0;
  events[0].data.u64 = 0;
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 1, 1));
  observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

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
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

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
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EpollEdgeTriggered) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.u64 = expected;
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  ASSERT_EQ(1, write(fds[1], "aa", 1));

  epoll_event events[2];
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

  char c;
  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  // no new writes yet
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 2, 1));

  ASSERT_EQ(1, write(fds[1], "a", 1));

  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));

  // event has been consumed
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 2, 1));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

// This identical to |EpollEdgeTriggered| but we make
// The fd ready prior to calling epoll_ctl.
TEST(EpollTest, EpollEdgeFdReady) {
  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  const uint64_t expected = 0x123456789abcdef0;

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.u64 = expected;

  // This makes fds[0] ready before calling epoll_ctl.
  ASSERT_EQ(1, write(fds[1], "aa", 1));

  // This should queue a packet immediately.
  ASSERT_NE(-1, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  epoll_event events[2];
  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));
  uint64_t observed = events[0].data.u64;
  ASSERT_EQ(expected, observed);

  char c;
  ASSERT_EQ(1, read(fds[0], &c, sizeof(c)));

  // no new writes yet
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 2, 1));

  ASSERT_EQ(1, write(fds[1], "a", 1));

  ASSERT_EQ(1, epoll_wait(epoll_fd, events, 2, 1));

  // event has been consumed
  ASSERT_EQ(0, epoll_wait(epoll_fd, events, 2, 1));

  close(fds[0]);
  close(fds[1]);
  close(epoll_fd);
}

TEST(EpollTest, EventFdEdgeTriggeredEveryWriteGeneratesEvent) {
  // Verify that we receive EPOLLIN for every new write
  // on an eventfd even if it is not read.
  int pollfd = epoll_create(1);
  ASSERT_NE(pollfd, -1);
  int evfd = eventfd(0, EFD_NONBLOCK);
  ASSERT_NE(evfd, -1);

  epoll_event ev;
  ev.events = EPOLLIN | EPOLLET;
  ev.data.ptr = nullptr;
  int ret = epoll_ctl(pollfd, EPOLL_CTL_ADD, evfd, &ev);
  ASSERT_NE(ret, -1);

  // On Linux, every iteration through the test loop produces a new EPOLLIN event.
#if defined(__linux__)
  constexpr int iteration_count = 10;
#else
  // https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=64286
  // Fuchsia currently does not implement this behavior correctly and only
  // produces an EPOLLIN event on the first write.
  constexpr int iteration_count = 1;
#endif

  for (int i = 0; i < iteration_count; ++i) {
    int n;
    do {
      n = eventfd_write(evfd, 1);
    } while (n < 0 && errno == EINTR);
    ASSERT_EQ(n, 0);

    struct epoll_event events[1];
    do {
      n = epoll_wait(pollfd, events, 1, 100);
    } while (n < 0 && errno == EINTR);
    ASSERT_EQ(n, 1);

    ASSERT_TRUE(events[0].events & EPOLLIN);
  }
}

TEST(EpollTest, CloseFileDescriptorInsideEpollSet) {
  // This tests a surprising behavior / design bug in epoll(). In Linux, entries
  // in the epoll interest set are registered on the file descriptor and not the
  // file description. This means close() which operates on a file descriptor,
  // does not actually remove the entry from the epoll set.

  // Illumos decided not to implement this quirk and instead registers file
  // descriptors into the epoll set instead of file descriptions:
  // https://illumos.org/man/5/epoll

  int fds[2];
  ASSERT_NE(-1, pipe(fds));

  int epoll_fd = epoll_create(1);
  ASSERT_NE(-1, epoll_fd);

  // Register the file description referred to by the file descriptor fds[0].
  epoll_event ev = {};
  ev.events = EPOLLIN;

  ASSERT_EQ(0, epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fds[0], &ev));

  // Duplicate fds[0] to produce a second file descriptor referring to the same file description.
  int duplicate = dup(fds[0]);
  ASSERT_NE(-1, duplicate);

  // Close the original file descriptor.
  ASSERT_EQ(0, close(fds[0]));

  // At this point the epoll entry is still active and cannot be removed from epoll_fd.
  EXPECT_EQ(0, epoll_wait(epoll_fd, &ev, 1, 0));

  char c = 'a';
  EXPECT_EQ(1, write(fds[1], &c, 1));

#if defined(__linux__)
  // TODO(https://bugs.fuchsia.dev/p/fuchsia/issues/detail?id=64296):
  // The current Fuchsia implementation does not receive this event.
  EXPECT_EQ(1, epoll_wait(epoll_fd, &ev, 1, 0));
  EXPECT_EQ(EPOLLIN, ev.events);
#endif  // defined(__linux__)

  close(epoll_fd);
  close(fds[1]);
  close(duplicate);
}
