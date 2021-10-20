// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "request_watchdog.h"

#include <lib/async-testing/test_loop.h>

#include <ostream>

#include <gtest/gtest.h>

namespace {

TEST(RequestWatchdog, ConstructDestruct) {
  async::TestLoop loop;
  RequestWatchdog<int> watchdog(loop.dispatcher());
}

TEST(RequestWatchdog, PollEmpty) {
  async::TestLoop loop;
  RequestWatchdog<int> watchdog(loop.dispatcher());
  loop.RunFor(RequestWatchdog<int>::kDefaultPollInterval * 10);
}

TEST(RequestWatchdog, CreateDestroyRequests) {
  async::TestLoop loop;
  RequestWatchdog<int> watchdog(loop.dispatcher());

  RequestWatchdog<int>::RequestToken request1 = watchdog.Start(0);
  RequestWatchdog<int>::RequestToken request2 = watchdog.Start(1);
  RequestWatchdog<int>::RequestToken request3 = watchdog.Start(2);

  loop.RunFor(RequestWatchdog<int>::kDefaultPollInterval * 10);

  request3.reset();
  request1.reset();
  request2.reset();
}

// PrintCounter records how many times it is printed via the ostream operator<<.
struct PrintCounter {
  size_t* count;

  friend std::ostream& operator<<(std::ostream& os, const PrintCounter& status) {
    (*status.count)++;
    os << "<PrintCounter>";
    return os;
  }
};

TEST(RequestWatchdog, LogOnDeadlineExceeded) {
  async::TestLoop loop;
  RequestWatchdog<PrintCounter> watchdog(loop.dispatcher());
  size_t print_count = 0;

  auto request = watchdog.Start(PrintCounter{&print_count});

  // Status should not be printed prior to the deadline.
  loop.RunFor(RequestWatchdog<PrintCounter>::kDefaultDeadline - zx::sec(1));
  EXPECT_EQ(print_count, 0u);

  // Status should be printed once we hit the deadline, though.
  loop.RunFor(zx::sec(2));
  EXPECT_EQ(print_count, 1u);

  // Once printed, it should be printed again.
  loop.RunFor(RequestWatchdog<int>::kDefaultDeadline * 10);
  EXPECT_EQ(print_count, 1u);
}

TEST(RequestWatchdog, MoveRequest) {
  async::TestLoop loop;
  RequestWatchdog<PrintCounter> watchdog(loop.dispatcher());
  size_t print_count = 0;

  RequestWatchdog<PrintCounter>::RequestToken request = watchdog.Start(PrintCounter{&print_count});
  RequestWatchdog<PrintCounter>::RequestToken other;

  // Move from `request` to `other`.
  other = std::move(request);
  request.reset();

  // RequestToken should be active, and print out a warning when appropriate.
  loop.RunFor(RequestWatchdog<int>::kDefaultDeadline + zx::sec(1));
  EXPECT_EQ(print_count, 1u);
}

}  // namespace
