// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/test_with_message_loop.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace gtest {

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

bool RunGivenLoopUntilWithTimeout(fsl::MessageLoop* message_loop,
                                  std::function<bool()> condition,
                                  fxl::TimeDelta timeout,
                                  fxl::TimeDelta step) {
  const fxl::TimePoint deadline = timeout == fxl::TimeDelta::FromSeconds(0)
                                      ? fxl::TimePoint::Max()
                                      : fxl::TimePoint::Now() + timeout;
  while (fxl::TimePoint::Now() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(message_loop, step);
  }
  return condition();
}

}  // namespace

TestWithMessageLoop::TestWithMessageLoop() = default;

TestWithMessageLoop::~TestWithMessageLoop() = default;

void TestWithMessageLoop::RunLoop() {
  message_loop_.Run();
}

bool TestWithMessageLoop::RunLoopWithTimeout(fxl::TimeDelta timeout) {
  return RunGivenLoopWithTimeout(&message_loop_, timeout);
}

bool TestWithMessageLoop::RunLoopUntilWithTimeout(
    std::function<bool()> condition,
    fxl::TimeDelta timeout,
    fxl::TimeDelta step) {
  return RunGivenLoopUntilWithTimeout(&message_loop_, std::move(condition),
                                      timeout, step);
}

bool TestWithMessageLoop::RunLoopUntil(std::function<bool()> condition,
                                       fxl::TimeDelta step) {
  return RunGivenLoopUntilWithTimeout(&message_loop_, std::move(condition),
                                      fxl::TimeDelta::FromSeconds(0), step);
}

void TestWithMessageLoop::RunLoopUntilIdle() {
  message_loop_.RunUntilIdle();
}

fxl::Closure TestWithMessageLoop::MakeQuitTask() {
  return [this] { message_loop_.PostQuitTask(); };
}

fxl::Closure TestWithMessageLoop::MakeQuitTaskOnce() {
  return fxl::MakeCopyable([this, called = false]() mutable {
    if (!called) {
      called = true;
      message_loop_.PostQuitTask();
    }
  });
}

}  // namespace gtest
