// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/gtest/real_loop_fixture.h"

#include <lib/async/cpp/task.h>

namespace gtest {

RealLoopFixture::RealLoopFixture() : loop_(&kAsyncLoopConfigAttachToThread) {}

RealLoopFixture::~RealLoopFixture() = default;

async_dispatcher_t* RealLoopFixture::dispatcher() { return loop_.dispatcher(); }

void RealLoopFixture::RunLoop() {
  loop_.Run();
  loop_.ResetQuit();
}

bool RealLoopFixture::RunLoopWithTimeout(zx::duration timeout) {
  zx_status_t status = ZX_OK;

  const zx::time timeout_deadline = zx::deadline_after(timeout);

  while (zx::clock::get_monotonic() < timeout_deadline &&
         loop_.GetState() == ASYNC_LOOP_RUNNABLE) {
    status = loop_.Run(timeout_deadline, false);
  }

  loop_.ResetQuit();
  return status == ZX_ERR_TIMED_OUT;
}

bool RealLoopFixture::RunLoopWithTimeoutOrUntil(fit::function<bool()> condition,
                                                zx::duration timeout,
                                                zx::duration step) {
  const zx::time timeout_deadline = zx::deadline_after(timeout);

  while (zx::clock::get_monotonic() < timeout_deadline &&
         loop_.GetState() == ASYNC_LOOP_RUNNABLE) {
    if (condition()) {
      loop_.ResetQuit();
      return true;
    }

    if (step == zx::duration::infinite()) {
      // Performs a single unit of work, possibly blocking until there is work
      // to do or the timeout deadline arrives.
      loop_.Run(timeout_deadline, true);
    } else {
      // Performs work until the step deadline arrives.
      loop_.Run(zx::deadline_after(step), false);
    }
  }

  loop_.ResetQuit();
  return condition();
}

void RealLoopFixture::RunLoopUntil(fit::function<bool()> condition,
                                   zx::duration step) {
  RunLoopWithTimeoutOrUntil(std::move(condition), zx::duration::infinite(),
                            step);
}

void RealLoopFixture::RunLoopUntilIdle() {
  loop_.RunUntilIdle();
  loop_.ResetQuit();
}

void RealLoopFixture::QuitLoop() { loop_.Quit(); }

fit::closure RealLoopFixture::QuitLoopClosure() {
  return [this] { loop_.Quit(); };
}

}  // namespace gtest
