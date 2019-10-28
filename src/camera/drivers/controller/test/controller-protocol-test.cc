// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller-protocol.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>

#include <fbl/auto_call.h>

#include "fake_isp.h"
#include "src/camera/drivers/controller/camera_pipeline_manager.h"
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
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator1_.NewRequest()));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator2_.NewRequest()));
    isp_ = fake_isp_.client();
    controller_protocol_device_ =
        std::make_unique<ControllerImpl>(isp_, std::move(sysmem_allocator1_));
    camera_pipeline_manager_ =
        std::make_unique<CameraPipelineManager>(isp_, std::move(sysmem_allocator2_));
  }

  void TearDown() override {
    camera_client_ = nullptr;
    context_ = nullptr;
    sysmem_allocator1_ = nullptr;
    sysmem_allocator2_ = nullptr;
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

  void TestDebugStreamConfigNode() {
    controller_protocol_device_->PopulateConfigurations();
    InternalConfigInfo* info = nullptr;
    // Get internal configuration for debug config
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(kDebugConfig, &info));
    // Get stream config for fuchsia::camera2::CameraStreamType::FULL_RESOLUTION; stream
    auto stream_config = controller_protocol_device_->GetStreamConfigNode(
        info, fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
    EXPECT_NE(nullptr, stream_config);

    stream_config = controller_protocol_device_->GetStreamConfigNode(
        info, fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION);
    EXPECT_EQ(nullptr, stream_config);
  }

  void TestConfigureInputNode_DebugConfig() {
    controller_protocol_device_->PopulateConfigurations();
    InternalConfigInfo* internal_info = nullptr;
    // Get internal configuration for debug config
    EXPECT_EQ(ZX_OK,
              controller_protocol_device_->GetInternalConfiguration(kDebugConfig, &internal_info));
    // Get stream config for fuchsia::camera2::CameraStreamType::FULL_RESOLUTION; stream
    auto stream_config_node = controller_protocol_device_->GetStreamConfigNode(
        internal_info, fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
    EXPECT_NE(nullptr, stream_config_node);

    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);

    CameraPipelineInfo info;
    info.output_buffers = std::move(buffer_collection);
    info.image_format_index = 0;
    info.node = *stream_config_node;
    info.stream_config = &stream_config;

    std::shared_ptr<CameraProcessNode> out_processing_node;
    EXPECT_EQ(ZX_OK, camera_pipeline_manager_->ConfigureInputNode(&info, &out_processing_node));

    EXPECT_NE(nullptr, out_processing_node->isp_stream_protocol());
    EXPECT_EQ(NodeType::kInputStream, out_processing_node->type());
  }

  FakeIsp fake_isp_;
  async::Loop loop_;
  std::unique_ptr<ControllerImpl> controller_protocol_device_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<camera::CameraPipelineManager> camera_pipeline_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator1_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator2_;
  ddk::IspProtocolClient isp_;
};

TEST_F(ControllerProtocolTest, GetConfigs) { TestInternalConfigs(); }

TEST_F(ControllerProtocolTest, GetDebugStreamConfig) { TestDebugStreamConfigNode(); }

TEST_F(ControllerProtocolTest, ConfigureInputNodeDebugConfig) {
  TestConfigureInputNode_DebugConfig();
}

}  // namespace camera
