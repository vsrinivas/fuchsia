// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/real_loop_fixture.h"

#include <lib/async/cpp/task.h>

namespace gtest {

namespace {

bool RunGivenLoopWithTimeout(async::Loop* loop, zx::duration timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(
      loop->async(),
      [loop, canceled, &timed_out] {
        if (*canceled) {
          return;
        }
        timed_out = true;
        loop->Quit();
      },
      timeout);
  loop->Run();
  loop->ResetQuit();
  // Another task can call Quit() on the message loop, which exits the
  // message loop before the delayed task executes, in which case |timed_out| is
  // still false here because the delayed task hasn't run yet.
  // Since the message loop isn't destroyed then (as it usually would after
  // Quit()), and presumably can be reused after this function returns we
  // still need to prevent the delayed task to quit it again at some later time
  // using the canceled pointer.
  if (!timed_out) {
    *canceled = true;
  }
  return timed_out;
}

bool RunGivenLoopWithTimeoutOrUntil(async::Loop* loop,
                                   fit::function<bool()> condition,
                                   zx::duration timeout,
                                   zx::duration step) {
  const zx::time deadline = (timeout == zx::sec(0))
                            ? zx::time::infinite()
                            : zx::deadline_after(timeout);
  while (zx::clock::get(ZX_CLOCK_MONOTONIC) < deadline) {
    if (condition()) {
      return true;
    }
    RunGivenLoopWithTimeout(loop, step);
  }
  return condition();
}

}  // namespace

RealLoopFixture::RealLoopFixture() : loop_(&kAsyncLoopConfigMakeDefault) {}

RealLoopFixture::~RealLoopFixture() = default;

async_t* RealLoopFixture::dispatcher() { return loop_.async(); }

void RealLoopFixture::RunLoop() {
  loop_.Run();
  loop_.ResetQuit();
}

bool RealLoopFixture::RunLoopWithTimeout(zx::duration timeout) {
  return RunGivenLoopWithTimeout(&loop_, timeout);
}

bool RealLoopFixture::RunLoopWithTimeoutOrUntil(
    fit::function<bool()> condition,
    zx::duration timeout,
    zx::duration step) {
  return RunGivenLoopWithTimeoutOrUntil(&loop_, std::move(condition),
                                       timeout, step);
}

bool RealLoopFixture::RunLoopUntil(fit::function<bool()> condition,
                                   zx::duration step) {
  return RunGivenLoopWithTimeoutOrUntil(&loop_, std::move(condition),
                                        zx::sec(0), step);
}

void RealLoopFixture::RunLoopUntilIdle() {
  loop_.RunUntilIdle();
  loop_.ResetQuit();
}

void RealLoopFixture::QuitLoop() {
  loop_.Quit();
}

fit::closure RealLoopFixture::QuitLoopClosure() {
  return [this] { loop_.Quit(); };
}

}  // namespace gtest
