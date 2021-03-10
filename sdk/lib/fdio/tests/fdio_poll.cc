// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <unistd.h>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

namespace {

// While POSIX doesn't spell out the behavior of polling on 0 fds
// specifically, we guarantee that it is equivalent to a sleep.  There
// are a number of reasons for this choice.
//
// 1. This matches many people's mental model of the operation. That
//    is: poll returns before its timeout if one of the given fds has
//    an event, or if a signal occurs; when here are no fds to have
//    events, there can be no events!
//
// 2. This allows the idiom of poll(NULL, 0, timeout) to work on fdio.
//    Even if the historical motivations for this idiom on other
//    UNIX-like systems do not apply to fdio, the source compatibility
//    is desirable.
//
// 3. This matches the behavior of Zircon's zx_object_wait_many. This
//    is desirable both from an implementation perspective, and for
//    the sake of those reasoning about waiting on things in different
//    forms on Fuchsia.
//
// 4. Generally, we have biased towards providing a posix-lite
//    interface that, when ambiguities arise, resembles Linux more
//    than other systems. Given that fdio provides ppoll, which
//    historically was a Linux-only syscall (recent FreeBSDs also have
//    it), it is most consistent, and most useful to our clients, to
//    do what Linux does in its poll semantics.

// These tests use poll/ppoll to sleep, and check both the return
// value, and that the an appropriate amount of time has passed.
//
// Since the point of these tests is not to establish that the
// underlying primitives sleep for the correct amount of time, the
// assertion about the amount of time advanced is pretty loose. We
// instruct poll/ppoll sleep for 10 milliseconds, and assert that at
// least 1 millisecond has elapsed. With those values, it is
// implausible that execution fell straight through the body of
// poll/ppoll without blocking, while also allowing this test to not
// have to reason about the precise guarantees made by the monotonic
// clock or blocking system calls.

TEST(Poll, PollZeroFds) {
  constexpr std::chrono::duration minimum_duration = std::chrono::milliseconds(1);

  const auto begin = std::chrono::steady_clock::now();
  ASSERT_EQ(poll(nullptr, 0, std::chrono::milliseconds(minimum_duration).count()), 0, "%s",
            strerror(errno));
  ASSERT_GE(std::chrono::steady_clock::now() - begin, minimum_duration);
}

TEST(Poll, PPollZeroFds) {
  constexpr std::chrono::duration minimum_duration = std::chrono::milliseconds(1);

  const struct timespec timeout = {
      .tv_nsec = std::chrono::nanoseconds(minimum_duration).count(),
  };
  const auto begin = std::chrono::steady_clock::now();
  ASSERT_EQ(ppoll(nullptr, 0, &timeout, nullptr), 0, "%s", strerror(errno));
  ASSERT_GE(std::chrono::steady_clock::now() - begin, minimum_duration);
}

TEST(Poll, PPollNegativeTimeout) {
  {
    const struct timespec timeout_ts = {
        .tv_sec = -1,
    };
    ASSERT_EQ(ppoll(nullptr, 0, &timeout_ts, nullptr), -1);
    ASSERT_EQ(errno, EINVAL, "%s", strerror(errno));
  }

  {
    const struct timespec timeout_ts = {
        .tv_nsec = -1,
    };
    ASSERT_EQ(ppoll(nullptr, 0, &timeout_ts, nullptr), -1);
    ASSERT_EQ(errno, EINVAL, "%s", strerror(errno));
  }
}

TEST(Poll, PPollInvalidTimeout) {
  const struct timespec timeout_ts = {
      .tv_nsec = 1000000000,
  };
  ASSERT_EQ(ppoll(nullptr, 0, &timeout_ts, nullptr), -1);
  ASSERT_EQ(errno, EINVAL, "%s", strerror(errno));
}

TEST(Poll, Pipe) {
  std::array<fbl::unique_fd, 2> fds;
  int int_fds[fds.size()];
  ASSERT_EQ(pipe(int_fds), 0, "%s", strerror(errno));
  fds[0].reset(int_fds[0]);
  fds[1].reset(int_fds[1]);

  constexpr int kTimeout = 10000;

  {
    char c;
    ASSERT_EQ(write(fds[1].get(), &c, sizeof(c)), sizeof(c), "%s", strerror(errno));
    struct pollfd pfds[] = {{
                                .fd = fds[0].get(),
                                .events = POLLIN,
                            },
                            {
                                .fd = fds[1].get(),
                                .events = POLLOUT,
                            }};
    int n = poll(pfds, std::size(pfds), kTimeout);
    ASSERT_GE(n, 0, "%s", strerror(errno));
    ASSERT_EQ(n, std::size(pfds));
    ASSERT_EQ(pfds[0].revents, POLLIN);
    ASSERT_EQ(pfds[1].revents, POLLOUT);
  }

  {
    ASSERT_EQ(close(fds[1].get()), 0, "%s", strerror(errno));
    struct pollfd pfds[] = {
        {
            .fd = fds[0].get(),
#if defined(__Fuchsia__)
            // TODO(https://fxbug.dev/47132): For Linux parity, pipe wait_begin needs to always wait
            // on ZXIO_SIGNAL_PEER_CLOSED and wait_end needs to set POLLHUP on seeing this.
            .events = POLLIN,
#endif
        },
        {
            .fd = fds[1].get(),
        }};
    int n = poll(pfds, std::size(pfds), kTimeout);
    ASSERT_GE(n, 0, "%s", strerror(errno));
    ASSERT_EQ(n, std::size(pfds));
    ASSERT_EQ(pfds[0].revents,
#if defined(__Fuchsia__)
              // TODO(https://fxbug.dev/47132): For Linux parity, pipe wait_begin needs to always
              // wait on ZXIO_SIGNAL_PEER_CLOSED and wait_end needs to set POLLHUP on seeing this.
              POLLIN
#else
              POLLHUP
#endif
    );
    ASSERT_EQ(pfds[1].revents, POLLNVAL);
  }
}

}  // namespace
