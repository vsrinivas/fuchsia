
// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/gtest/test_loop_fixture.h>
#include <zircon/errors.h>

#include "fuchsia/sysmem/cpp/fidl.h"
#include "src/camera/lib/fake_stream/fake_stream.h"

class FakeStreamTest : public gtest::TestLoopFixture {
 protected:
  void SetUp() override {
    auto result = camera::FakeStream::Create(stream_.NewRequest(), dispatcher());
    ASSERT_TRUE(result.is_ok());
    fake_stream_ = result.take_value();
    stream_.set_error_handler(
        [](zx_status_t status) { ADD_FAILURE() << "Stream server disconnected: " << status; });
    stream_.events().OnFrameAvailable = [this](fuchsia::camera2::FrameAvailableInfo info) {
      stream_->ReleaseFrame(info.buffer_id);
      frames_.push_back(std::move(info));
    };
    stream_->Start();
    RunLoopUntilIdle();
  }

  void TearDown() override {
    stream_ = nullptr;
    fake_stream_ = nullptr;
  }

  fuchsia::camera2::StreamPtr stream_;
  std::unique_ptr<camera::FakeStream> fake_stream_;
  std::vector<fuchsia::camera2::FrameAvailableInfo> frames_;
};

// Conformant Stream client.
TEST_F(FakeStreamTest, GoodClient) {
  bool callback_called = false;
  stream_->GetImageFormats([&](std::vector<fuchsia::sysmem::ImageFormat_2> formats) {
    callback_called = true;
    ASSERT_GT(formats.size(), 0u);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  EXPECT_EQ(frames_.size(), 0u);
  const fuchsia::camera2::FrameAvailableInfo kFrame{
      .frame_status = fuchsia::camera2::FrameStatus::OK,
      .buffer_id = 42,
  };
  fuchsia::camera2::FrameAvailableInfo frame_copy;
  ASSERT_EQ(kFrame.Clone(&frame_copy), ZX_OK);
  EXPECT_EQ(fake_stream_->SendFrameAvailable(std::move(frame_copy)), ZX_OK);
  RunLoopUntilIdle();
  ASSERT_EQ(frames_.size(), 1u);
  EXPECT_EQ(frames_[0].frame_status, kFrame.frame_status);
  EXPECT_EQ(frames_[0].buffer_id, kFrame.buffer_id);

  callback_called = false;
  stream_->SetImageFormat(0, [&](zx_status_t status) {
    callback_called = true;
    EXPECT_EQ(status, ZX_OK);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  stream_->SetRegionOfInterest(0, 0, 1, 1, [&](zx_status_t status) {
    callback_called = true;
    EXPECT_EQ(status, ZX_OK);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  auto result = fake_stream_->StreamClientStatus();
  EXPECT_TRUE(result.is_ok());
}

// Calls Start while started.
TEST_F(FakeStreamTest, BadClient1) {
  stream_->Start();
  RunLoopUntilIdle();
  auto result = fake_stream_->StreamClientStatus();
  ASSERT_TRUE(result.is_error());
  std::cerr << result.error() << std::endl;
}

// Releases an unheld frame.
TEST_F(FakeStreamTest, BadClient2) {
  stream_->ReleaseFrame(0);
  RunLoopUntilIdle();
  auto result = fake_stream_->StreamClientStatus();
  ASSERT_TRUE(result.is_error());
  std::cerr << result.error() << std::endl;
}

// Invalid region of interest.
TEST_F(FakeStreamTest, BadClient3) {
  bool callback_called = false;
  stream_->SetRegionOfInterest(1, 1, 0, 0, [&](zx_status_t status) {
    callback_called = true;
    EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
  auto result = fake_stream_->StreamClientStatus();
  ASSERT_TRUE(result.is_error());
  std::cerr << result.error() << std::endl;
}
