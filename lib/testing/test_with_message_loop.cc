// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/lib/testing/test_with_message_loop.h"

#include "lib/fxl/logging.h"

namespace modular {
namespace testing {

// Must be a pointer so static initialization is safe.
fsl::MessageLoop* TestWithMessageLoop::message_loop_{};

namespace {

bool RunGivenLoopWithTimeout(fsl::MessageLoop* message_loop,
                             fxl::TimeDelta timeout) {
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

bool RunGivenLoopUntil(fsl::MessageLoop* message_loop,
                       std::function<bool()> condition,
                       fxl::TimeDelta timeout) {
  if (condition()) {
    FXL_LOG(ERROR) << "|condition| is already true prior to running the loop.";
    return false;
  }

  const fxl::TimePoint deadline = fxl::TimePoint::Now() + timeout;
  while (fxl::TimePoint::Now() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(message_loop, fxl::TimeDelta::FromMilliseconds(10));
  }
  return condition();
}

}  // namespace

TestWithMessageLoop::TestWithMessageLoop() {
  if (message_loop_ == nullptr) {
    message_loop_ = new fsl::MessageLoop;
  } else {
    FXL_CHECK(message_loop_ == fsl::MessageLoop::GetCurrent());
  }
}

TestWithMessageLoop::~TestWithMessageLoop() = default;

bool TestWithMessageLoop::RunLoopWithTimeout(fxl::TimeDelta timeout) {
  return RunGivenLoopWithTimeout(message_loop_, timeout);
}

bool TestWithMessageLoop::RunLoopUntil(std::function<bool()> condition,
                                       fxl::TimeDelta timeout) {
  return RunGivenLoopUntil(message_loop_, std::move(condition), timeout);
}

std::function<void()> TestWithMessageLoop::MakeQuitTask() {
  return [] { message_loop_->PostQuitTask(); };
}

}  // namespace testing
}  // namespace modular
