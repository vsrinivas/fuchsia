// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "peridot/bin/ledger/test/test_with_message_loop.h"

#include "lib/fxl/functional/make_copyable.h"
#include "lib/fxl/logging.h"

namespace test {

bool RunGivenLoopWithTimeout(fsl::MessageLoop* message_loop,
                             fxl::TimeDelta timeout) {
  auto canceled = std::make_unique<bool>(false);
  bool* canceled_ptr = canceled.get();
  bool timed_out = false;
  message_loop->task_runner()->PostDelayedTask(
      fxl::MakeCopyable(
          [message_loop, canceled = std::move(canceled), &timed_out] {
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

bool RunGivenLoopUntil(fsl::MessageLoop* message_loop,
                       std::function<bool()> condition,
                       fxl::TimeDelta timeout,
                       fxl::TimeDelta step) {
  const fxl::TimePoint deadline = fxl::TimePoint::Now() + timeout;
  while (fxl::TimePoint::Now() < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(message_loop, step);
  }
  return condition();
}

bool TestWithMessageLoop::RunLoopWithTimeout(fxl::TimeDelta timeout) {
  return test::RunGivenLoopWithTimeout(&message_loop_, timeout);
}

bool TestWithMessageLoop::RunLoopUntil(std::function<bool()> condition,
                                       fxl::TimeDelta timeout,
                                       fxl::TimeDelta step) {
  return test::RunGivenLoopUntil(&message_loop_, std::move(condition), timeout,
                                 step);
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

}  // namespace test
