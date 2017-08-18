// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/modular/lib/testing/test_with_message_loop.h"

#include "lib/ftl/logging.h"

namespace modular {
namespace testing {
namespace {

bool RunGivenLoopWithTimeout(mtl::MessageLoop* message_loop,
                             ftl::TimeDelta timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  message_loop->task_runner()->PostDelayedTask(
      [message_loop, canceled, &timed_out] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        message_loop->QuitNow();
      },
      timeout);
  message_loop->Run();
  // Another task can call QuitNow() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet.
  // Since the message loop isn't destroyed then (as it usually would after
  // QuitNow()), and presumably can be reused after this function returns we
  // still need to prevent the delayed task to quit it again at some later time
  // using the canceled pointer.
  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}

bool RunGivenLoopUntil(mtl::MessageLoop* message_loop,
                       std::function<bool()> condition,
                       ftl::TimeDelta timeout) {
  if (condition()) {
    FTL_LOG(ERROR) << "|condition| is already true prior to running the loop.";
    return false;
  }

  const ftl::TimePoint deadline = ftl::TimePoint::Now() + timeout;
  while (ftl::TimePoint::Now() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(message_loop, ftl::TimeDelta::FromMilliseconds(10));
  }
  return condition();
}

}  // namespace

bool TestWithMessageLoop::RunLoopWithTimeout(ftl::TimeDelta timeout) {
  return RunGivenLoopWithTimeout(&message_loop_, timeout);
}

bool TestWithMessageLoop::RunLoopUntil(std::function<bool()> condition,
                                       ftl::TimeDelta timeout) {
  return RunGivenLoopUntil(&message_loop_, std::move(condition), timeout);
}

std::function<void()> TestWithMessageLoop::MakeQuitTask() {
  return [this] { message_loop_.PostQuitTask(); };
}

}  // namespace testing
}  // namespace modular
