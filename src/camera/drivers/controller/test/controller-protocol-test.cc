// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../controller-protocol.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>

#include <fbl/auto_call.h>
#include <src/lib/fxl/logging.h>
#include <zxtest/zxtest.h>

#include "../controller-device.h"

// NOTE: In this test, we are actually just unit testing the ControllerImpl class

namespace camera {

namespace {
constexpr uint32_t kDebugConfig = 0;
constexpr uint32_t kMonitorConfig = 1;
}  // namespace
class ControllerProtocolTest : public camera::ControllerImpl {
 public:
  explicit ControllerProtocolTest(async::Loop* loop,
                                  fuchsia::camera2::hal::ControllerSyncPtr* camera_client)
      : ControllerImpl(camera_client->NewRequest(), loop->dispatcher(), [] {}) {
    zx_status_t status = loop->StartThread("camera-controller-loop");
    EXPECT_EQ(status, ZX_OK);
  }

  void TestInternalConfigs() {
    InternalConfigInfo* info = nullptr;

    // Invalid config index and pointer
    EXPECT_EQ(GetInternalConfiguration(0, &info), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(GetInternalConfiguration(0, nullptr), ZX_ERR_INVALID_ARGS);

    PopulateConfigurations();

    // Debug Configuration
    EXPECT_OK(GetInternalConfiguration(kDebugConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 1);
    // 1st stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type,
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
    // FR supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 1);
    EXPECT_EQ(info->streams_info[0].supported_streams[0],
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

    // Monitor Configuration
    EXPECT_OK(GetInternalConfiguration(kMonitorConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 2);

    // 1st Stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type,
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

    // FR Supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 2);
    EXPECT_EQ(info->streams_info[0].supported_streams[0],
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);
    EXPECT_EQ(info->streams_info[0].supported_streams[1],
              fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION |
                  fuchsia::camera2::CameraStreamType::MACHINE_LEARNING);

    // 2nd Stream is DS
    EXPECT_EQ(info->streams_info[1].input_stream_type,
              fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION);

    // DS supported streams
    EXPECT_EQ(info->streams_info[1].supported_streams.size(), 1);
    EXPECT_EQ(info->streams_info[1].supported_streams[0],
              fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  }

 private:
  std::vector<fuchsia::camera2::hal::Config> configs_;
};

TEST(ControllerProtocolTest, GetConfigs) {
  async::Loop local_loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  fuchsia::camera2::hal::ControllerSyncPtr camera_client;
  ControllerProtocolTest protocol(&local_loop, &camera_client);
  protocol.TestInternalConfigs();
}

}  // namespace camera
