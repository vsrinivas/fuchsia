// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include <gtest/gtest.h>

#ifndef SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_TEST_STREAM_TESTER_H_
#define SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_TEST_STREAM_TESTER_H_

namespace camera {

// This class takes a stream channel and runs tests on it.
class StreamTester {
 public:
  explicit StreamTester(zx::channel stream);

  ~StreamTester() { loop_.Shutdown(); }

  // Just tests that the channel can give 10 frames
  void TestGetFrames();

  // Resets the stream state, so other tests can be run.
  void ResetStream();

 private:
  void DefaultOnFrameAvailable(fuchsia::camera2::FrameAvailableInfo frame);

  async::Loop loop_;
  fuchsia::camera2::StreamPtr stream_;
  uint32_t frame_counter_ = 0;
  uint32_t buffer_full_counter_ = 0;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_VIRTUAL_CAMERA_TEST_STREAM_TESTER_H_
