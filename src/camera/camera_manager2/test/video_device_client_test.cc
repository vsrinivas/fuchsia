// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../video_device_client.h"

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <fuchsia/sysmem/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>

#include "fake_camera_controller.h"
#include "fake_logical_buffer_collection.h"
#include "gtest/gtest.h"
#include "src/lib/syslog/cpp/logger.h"
namespace camera {
namespace {

class VideoDeviceClientTest : public ::testing::Test {
 public:
  VideoDeviceClientTest()
      : fake_camera_(FakeController::StandardConfigs(), vdc_loop_.dispatcher()),
        fake_sysmem_(vdc_loop_.dispatcher()) {}

  ~VideoDeviceClientTest() override { vdc_loop_.Shutdown(); }

  void SetUp() override { vdc_loop_.StartThread(); }
  async::Loop vdc_loop_{&kAsyncLoopConfigNoAttachToCurrentThread};
  FakeController fake_camera_;
  FakeLogicalBufferCollection fake_sysmem_;
};

// Create VDC with interfacehandle
//   create with invalid handle
//   create, then close handle
//   give error in GetConfigs
//   give bad configs
TEST_F(VideoDeviceClientTest, CreateVDC) {
  // First: a successful create call:
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);
}

TEST_F(VideoDeviceClientTest, FailToCreateVDC) {
  // Give the VideoDeviceClient an InterfaceHandle that is not bound to a channel.
  // VideoClientDevice should recognize that the handle is invalid and fail to
  // be created.
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> bad_handle;
  ASSERT_EQ(VideoDeviceClient::Create(std::move(bad_handle)), nullptr);

  // Create a channel, close the other end and pass it into the VideoClientDevice.
  // VideoClientDevice should recognize that the handle is invalid and fail to
  // be created.
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> closed_handle;
  closed_handle.NewRequest().Close(ZX_ERR_ACCESS_DENIED);
  ASSERT_EQ(VideoDeviceClient::Create(std::move(closed_handle)), nullptr);

  // Repeat the test above, but close the channel by calling reset instead
  // of using the Close call.
  // VideoClientDevice should recognize that the handle is invalid and fail to
  // be created.
  fidl::InterfaceHandle<fuchsia::camera2::hal::Controller> closed_handle2;
  closed_handle2.NewRequest().TakeChannel().reset();
  ASSERT_EQ(VideoDeviceClient::Create(std::move(closed_handle2)), nullptr);
}

// Each of these three tests is seperated because we can only call GetCameraConnection
// once per test.
TEST_F(VideoDeviceClientTest, CreateVdcBadConfigReturnsError) {
  fake_camera_.set_configs_failure(FakeController::GetConfigsFailureMode::RETURNS_ERROR);
  ASSERT_EQ(VideoDeviceClient::Create(fake_camera_.GetCameraConnection()), nullptr);
}

TEST_F(VideoDeviceClientTest, CreateVdcBadConfigEmptyVector) {
  fake_camera_.set_configs_failure(FakeController::GetConfigsFailureMode::EMPTY_VECTOR);
  ASSERT_EQ(VideoDeviceClient::Create(fake_camera_.GetCameraConnection()), nullptr);
}

TEST_F(VideoDeviceClientTest, CreateVdcBadConfigInvalid) {
  fake_camera_.set_configs_failure(FakeController::GetConfigsFailureMode::INVALID_CONFIG);
  ASSERT_EQ(VideoDeviceClient::Create(fake_camera_.GetCameraConnection()), nullptr);
}

// MatchConstraints
TEST_F(VideoDeviceClientTest, FailToMatchConstraints) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);

  uint32_t config_index, stream_type;
  fuchsia::camera2::StreamConstraints constraints;
  // Constraints are empty, should fail:
  EXPECT_NE(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));

  // Add properties, but no stream type:
  fuchsia::camera2::StreamProperties empty_properties;
  constraints.set_properties(std::move(empty_properties));
  EXPECT_NE(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));

  // Set an unsupported type:
  fuchsia::camera2::StreamProperties properties4;
  properties4.set_stream_type(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION);
  constraints.set_properties(std::move(properties4));
  EXPECT_NE(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));
}

TEST_F(VideoDeviceClientTest, MatchConstraints) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);
  // Match the stream types set up in StandardConfig().
  // These tests are each called individually (instead of being part of a loop)
  // for debugging ease, should any of them fail.
  uint32_t config_index, stream_type;
  fuchsia::camera2::StreamConstraints constraints;
  fuchsia::camera2::StreamProperties properties;
  properties.set_stream_type(fuchsia::camera2::CameraStreamType::MONITORING);
  constraints.set_properties(std::move(properties));
  EXPECT_EQ(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));
  // With standard config:
  // Config  | stream index | stream type
  //   0           0           MACHINE_LEARNING + MONITORING
  //   0           1           MONITORING
  //   1           0           MACHINE_LEARNING + VIDEO_CONFERENCE
  //   1           1           VIDEO_CONFERENCE
  EXPECT_EQ(config_index, 0u);
  EXPECT_EQ(stream_type, 1u);

  fuchsia::camera2::StreamProperties properties1;
  properties1.set_stream_type(fuchsia::camera2::CameraStreamType::MONITORING |
                              fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  constraints.set_properties(std::move(properties1));
  EXPECT_EQ(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));
  EXPECT_EQ(config_index, 0u);
  EXPECT_EQ(stream_type, 0u);

  fuchsia::camera2::StreamProperties properties2;
  properties2.set_stream_type(fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE |
                              fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
  constraints.set_properties(std::move(properties2));
  EXPECT_EQ(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));
  EXPECT_EQ(config_index, 1u);
  EXPECT_EQ(stream_type, 0u);

  fuchsia::camera2::StreamProperties properties3;
  properties3.set_stream_type(fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  constraints.set_properties(std::move(properties3));
  EXPECT_EQ(ZX_OK, vdc->MatchConstraints(constraints, &config_index, &stream_type));
  EXPECT_EQ(config_index, 1u);
  EXPECT_EQ(stream_type, 1u);
}

TEST_F(VideoDeviceClientTest, CreateStream) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  vdc_loop_.RunUntilIdle();
  ASSERT_NE(vdc, nullptr);

  uint32_t config_index = 0, stream_type = 0, image_format_index = 0;
  auto buffer_collection = fake_sysmem_.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream;
  EXPECT_EQ(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection),
                                     stream.NewRequest(vdc_loop_.dispatcher())));
  while (fake_camera_.GetConnections() < 1) {
    vdc_loop_.RunUntilIdle();
  }
  EXPECT_TRUE(fake_camera_.HasMatchingChannel(stream.channel()));
}

TEST_F(VideoDeviceClientTest, CreateStreamBadSysmem) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);

  uint32_t config_index = 0, stream_type = 0, image_format_index = 0;
  fake_sysmem_.SetBufferError(true);
  auto buffer_collection = fake_sysmem_.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream;
  EXPECT_NE(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection),
                                     stream.NewRequest(vdc_loop_.dispatcher())));
}

TEST_F(VideoDeviceClientTest, CreateStreamWithBadIndex) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);

  // Standard config only has up to 2 image formats per stream
  // Attempt to create a stream with an invalid image_format_index.
  // The CreateStream call should fail.
  uint32_t config_index = 0, stream_type = 0;
  uint32_t image_format_index = fuchsia::camera2::MAX_IMAGE_FORMATS;
  auto buffer_collection = fake_sysmem_.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream;
  EXPECT_NE(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection),
                                     stream.NewRequest(vdc_loop_.dispatcher())));

  // Attempt to create a stream with an invalid stream_index.
  // The CreateStream call should fail.
  FakeLogicalBufferCollection fake_sysmem2(vdc_loop_.dispatcher());
  config_index = 0, stream_type = fuchsia::camera2::hal::MAX_STREAMS, image_format_index = 0;
  auto buffer_collection2 = fake_sysmem2.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream2;
  EXPECT_NE(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection2),
                                     stream2.NewRequest(vdc_loop_.dispatcher())));

  // Attempt to create a stream with an invalid config_index.
  // The CreateStream call should fail.
  FakeLogicalBufferCollection fake_sysmem3(vdc_loop_.dispatcher());
  config_index = fuchsia::camera2::hal::MAX_CONFIGURATIONS, stream_type = 0, image_format_index = 0;
  auto buffer_collection3 = fake_sysmem3.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream3;
  EXPECT_NE(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection3),
                                     stream3.NewRequest(vdc_loop_.dispatcher())));
  // Shutdown the loop, because otherwise the bindings will be called after the
  // FakeLogicalBufferCollection destructs
  vdc_loop_.Shutdown();
}

// Make sure the old connections are dropped when new connections start.
TEST_F(VideoDeviceClientTest, CreateStreamRemoveOldCollections) {
  auto vdc = VideoDeviceClient::Create(fake_camera_.GetCameraConnection());
  ASSERT_NE(vdc, nullptr);

  uint32_t config_index = 0, stream_type = 0, image_format_index = 0;
  auto buffer_collection = fake_sysmem_.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream;
  EXPECT_EQ(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection),
                                     stream.NewRequest(vdc_loop_.dispatcher())));

  vdc_loop_.RunUntilIdle();

  // Now open another stream of the same config.
  FakeLogicalBufferCollection fake_sysmem2(vdc_loop_.dispatcher());
  config_index = 0, stream_type = 1, image_format_index = 0;
  auto buffer_collection2 = fake_sysmem2.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream2;
  EXPECT_EQ(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection2),
                                     stream2.NewRequest(vdc_loop_.dispatcher())));

  while (fake_camera_.GetConnections() < 2) {
    vdc_loop_.RunUntilIdle();
  }
  // Expect both streams to be running now.
  EXPECT_TRUE(fake_camera_.HasMatchingChannel(stream.channel()));
  EXPECT_TRUE(fake_camera_.HasMatchingChannel(stream2.channel()));

  // Now attempt to create a stream from a different config.
  // The VDC should close the previous logical buffer collection.
  FakeLogicalBufferCollection fake_sysmem3(vdc_loop_.dispatcher());
  config_index = 1, stream_type = 0, image_format_index = 0;
  auto buffer_collection3 = fake_sysmem3.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream3;
  EXPECT_EQ(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection3),
                                     stream3.NewRequest(vdc_loop_.dispatcher())));

  while (!fake_sysmem_.closed() || !fake_sysmem2.closed()) {
    vdc_loop_.RunUntilIdle();
  }
  // Both other streams should now have closed.
  EXPECT_TRUE(fake_sysmem_.closed());
  EXPECT_TRUE(fake_sysmem2.closed());

  // Now connect to the same config and stream type.
  // The VDC should close the previous logical buffer collection.
  FakeLogicalBufferCollection fake_sysmem4(vdc_loop_.dispatcher());
  config_index = 1, stream_type = 0, image_format_index = 0;
  auto buffer_collection4 = fake_sysmem4.GetBufferCollection();
  fuchsia::camera2::StreamPtr stream4;
  EXPECT_EQ(ZX_OK, vdc->CreateStream(config_index, stream_type, image_format_index,
                                     std::move(buffer_collection4),
                                     stream4.NewRequest(vdc_loop_.dispatcher())));

  while (!fake_sysmem3.closed()) {
    vdc_loop_.RunUntilIdle();
  }
  // That should have closed the third stream, since it was the same type.
  EXPECT_TRUE(fake_sysmem3.closed());
  // Shutdown the loop, because otherwise the bindings will be called after the
  // FakeLogicalBufferCollection destructs
  vdc_loop_.Shutdown();
}

}  // namespace
}  // namespace camera
