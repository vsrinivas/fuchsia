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

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr uint32_t kNumFramesToWaitFor = 10;
constexpr auto TAG = "virtual_camera";

static zx_status_t RunLoopUntil(async::Loop* loop, fit::function<bool()> condition) {
  while (!condition()) {
    zx_status_t status = loop->RunUntilIdle();
    if (status != ZX_OK) {
      EXPECT_EQ(status, ZX_OK) << "Failed to run loop.";
      return status;
    }
  }
  return ZX_OK;
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

  EXPECT_EQ(
      RunLoopUntil(
          &loop_, [this]() { return frame_counter_ + buffer_full_counter_ > kNumFramesToWaitFor; }),
      ZX_OK);

  stream_->Stop();
}

void StreamTester::DefaultOnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame) {
  FX_LOGST(INFO, TAG) << "Received FrameNotify Event " << frame_counter_
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
