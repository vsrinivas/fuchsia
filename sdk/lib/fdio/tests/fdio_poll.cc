// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <poll.h>
#include <time.h>

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

timespec now() {
  timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts;
}

timespec delta(const timespec& before, const timespec& after) {
  timespec ts = after;
  ts.tv_sec -= before.tv_sec;
  if (ts.tv_nsec < before.tv_nsec) {
    ts.tv_sec -= 1;
    ts.tv_nsec += 1000000000ll;
  }
  ts.tv_nsec -= before.tv_nsec;
  return ts;
}

TEST(Poll, PollZeroFds) {
  timespec before = now();
  // Ten millisecond timeout.
  int timeout = 10;
  int ret = poll(nullptr, 0, timeout);
  timespec after = now();

  // poll should have returned 0.
  ASSERT_EQ(ret, 0);

  // Time should have advanced, at least a millisecond.
  ASSERT_TRUE(before.tv_sec < after.tv_sec ||
              (before.tv_sec == after.tv_sec && before.tv_nsec <= after.tv_nsec));
  timespec interval = delta(before, after);
  ASSERT_TRUE(interval.tv_sec > 0 || interval.tv_nsec >= 1000000ll);
}

TEST(Poll, PpollZeroFds) {
  timespec before = now();
  // Ten millisecond timeout.
  const timespec timeout = {0, 10 * 1000 * 1000};
  int ret = ppoll(nullptr, 0, &timeout, nullptr);
  timespec after = now();

  // poll should have returned 0.
  ASSERT_EQ(ret, 0);

  // Time should have advanced, at least a millisecond.
  ASSERT_TRUE(before.tv_sec < after.tv_sec ||
              (before.tv_sec == after.tv_sec && before.tv_nsec <= after.tv_nsec));
  timespec interval = delta(before, after);
  ASSERT_TRUE(interval.tv_sec > 0 || interval.tv_nsec >= 1000000ll);
}

}  // namespace
