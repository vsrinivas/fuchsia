// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "../controller-protocol.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/auto_call.h>

#include "src/lib/fxl/logging.h"

// NOTE: In this test, we are actually just unit testing the ControllerImpl class

namespace camera {

namespace {
constexpr uint32_t kDebugConfig = 0;
constexpr uint32_t kMonitorConfig = 1;
}  // namespace

class ControllerProtocolTest : public gtest::TestLoopFixture {
 public:
  ControllerProtocolTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        context_(sys::ComponentContext::Create()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, loop_.StartThread("camera-controller-loop"));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator_.NewRequest()));
    controller_protocol_device_ =
        std::make_unique<ControllerImpl>(isp_, std::move(sysmem_allocator_));
  }

  void TearDown() override {
    camera_client_ = nullptr;
    context_ = nullptr;
    sysmem_allocator_ = nullptr;
    controller_protocol_device_ = nullptr;
    loop_.Shutdown();
  }

  void TestInternalConfigs() {
    InternalConfigInfo* info = nullptr;

    // Invalid config index and pointer
    EXPECT_EQ(controller_protocol_device_->GetInternalConfiguration(0, &info), ZX_ERR_INVALID_ARGS);
    EXPECT_EQ(controller_protocol_device_->GetInternalConfiguration(0, nullptr),
              ZX_ERR_INVALID_ARGS);

    controller_protocol_device_->PopulateConfigurations();

    // Debug Configuration
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(kDebugConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 1u);
    // 1st stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type,
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
    // FR supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[0].supported_streams[0],
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

    // Monitor Configuration
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(kMonitorConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 2u);

    // 1st Stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type,
              fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

    // FR Supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 2u);
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
    EXPECT_EQ(info->streams_info[1].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[1].supported_streams[0],
              fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE);
  }

  ddk::IspProtocolClient isp_;
  async::Loop loop_;
  std::unique_ptr<ControllerImpl> controller_protocol_device_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  std::unique_ptr<sys::ComponentContext> context_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator_;
};

TEST_F(ControllerProtocolTest, GetConfigs) { TestInternalConfigs(); }

}  // namespace camera
