// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "stream_tester.h"

#include <lib/async/cpp/task.h>
#include <lib/fit/function.h>
#include <lib/gtest/real_loop_fixture.h>
#include <lib/zx/clock.h>

#include <fbl/auto_call.h>
#include <fbl/function.h>
#include <gtest/gtest.h>

#include "src/lib/fxl/logging.h"

namespace camera {

static constexpr uint32_t kTimeoutStepMs = 10;
static constexpr uint32_t kFrameTimeoutSec = 5;
static constexpr uint32_t kNumFramesToWaitFor = 10;

// TODO(37566): Figure out if this is needed.  It basically does the same as
// async::Loop::Run(deadline).
static bool RunGivenLoopWithTimeout(async::Loop* loop, zx::duration timeout) {
  // This cannot be a local variable because the delayed task below can execute
  // after this function returns.
  auto canceled = std::make_shared<bool>(false);
  bool timed_out = false;
  async::PostDelayedTask(
      loop->dispatcher(),
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

static bool RunLoopWithTimeoutOrUntil(async::Loop* loop, fit::function<bool()> condition,
                                      zx::duration timeout) {
  const zx::time timeout_deadline = zx::deadline_after(timeout);
  auto step = zx::msec(kTimeoutStepMs);
  while (zx::clock::get_monotonic() < timeout_deadline && loop->GetState() == ASYNC_LOOP_RUNNABLE) {
    if (condition()) {
      loop->ResetQuit();
      return true;
    }

    // Performs work until the step deadline arrives.
    RunGivenLoopWithTimeout(loop, step);
  }

  loop->ResetQuit();
  return condition();
}

StreamTester::StreamTester(zx::channel stream) : loop_(&kAsyncLoopConfigNoAttachToCurrentThread) {
  stream_.Bind(std::move(stream), loop_.dispatcher());
  zx_status_t status = loop_.StartThread(nullptr);
  EXPECT_EQ(status, ZX_OK) << "Failed to StartThread for tests. status: " << status;
}

// Just tests that the channel can give 10 frames
void StreamTester::TestGetFrames() {
  ResetStream();
  stream_.events().OnFrameAvailable = fbl::BindMember(this, &StreamTester::DefaultOnFrameAvailable);
  stream_->Start();

  RunLoopWithTimeoutOrUntil(
      &loop_, [this]() { return frame_counter_ + buffer_full_counter_ > kNumFramesToWaitFor; },
      zx::sec(kFrameTimeoutSec));

  stream_->Stop();
}

void StreamTester::DefaultOnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame) {
  FXL_LOG(INFO) << "Received FrameNotify Event " << frame_counter_
                << " at index: " << frame.buffer_id;
  switch (frame.frame_status) {
    case fuchsia::camera2::FrameStatus::OK:
      stream_->ReleaseFrame(frame.buffer_id);
      frame_counter_++;
      break;
    case fuchsia::camera2::FrameStatus::ERROR_BUFFER_FULL:
      buffer_full_counter_++;
      stream_->AcknowledgeFrameError();
      break;
    case fuchsia::camera2::FrameStatus::ERROR_FRAME:
      // Throw an assert if this happens:
      ASSERT_NE(frame.frame_status, fuchsia::camera2::FrameStatus::ERROR_FRAME);
      break;
  }
}

void StreamTester::ResetStream() {
  stream_->Stop();
  loop_.RunUntilIdle();
  loop_.ResetQuit();
  frame_counter_ = 0;
  buffer_full_counter_ = 0;
}

}  // namespace camera
