// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/src/test/test_with_message_loop.h"

#include "lib/ftl/functional/make_copyable.h"
#include "lib/ftl/logging.h"

namespace test {

bool RunGivenLoopWithTimeout(mtl::MessageLoop* message_loop,
                             ftl::TimeDelta timeout) {
  auto canceled = std::make_unique<bool>(false);
  bool* canceled_ptr = canceled.get();
  bool timed_out = false;
  message_loop->task_runner()->PostDelayedTask(
      ftl::MakeCopyable(
          [ message_loop, canceled = std::move(canceled), &timed_out ] {
            if (*canceled) {
              return;
            }
            timed_out = true;
            message_loop->QuitNow();
          }),
      timeout);
  message_loop->Run();
  if (!timed_out) {
    *canceled_ptr = true;
  }
  return timed_out;
}

bool RunGivenLoopUntil(mtl::MessageLoop* message_loop,
                       std::function<bool()> condition,
                       ftl::TimeDelta timeout) {
  const ftl::TimePoint deadline = ftl::TimePoint::Now() + timeout;
  while (ftl::TimePoint::Now() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(message_loop, ftl::TimeDelta::FromMilliseconds(10));
  }
  return condition();
}

bool TestWithMessageLoop::RunLoopWithTimeout(ftl::TimeDelta timeout) {
  return test::RunGivenLoopWithTimeout(&message_loop_, timeout);
}

bool TestWithMessageLoop::RunLoopUntil(std::function<bool()> condition,
                                       ftl::TimeDelta timeout) {
  return test::RunGivenLoopUntil(&message_loop_, std::move(condition), timeout);
}

ftl::Closure TestWithMessageLoop::MakeQuitTask() {
  return [this] { message_loop_.PostQuitTask(); };
}

}  // namespace test
