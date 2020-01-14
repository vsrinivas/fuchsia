// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls.h>

#include <fbl/auto_call.h>

#include "fake_gdc.h"
#include "fake_isp.h"
#include "src/camera/drivers/controller/configs/sherlock/sherlock_configs.h"
#include "src/camera/drivers/controller/controller-protocol.h"
#include "src/camera/drivers/controller/graph_utils.h"
#include "src/camera/drivers/controller/isp_stream_protocol.h"
#include "src/camera/drivers/controller/pipeline_manager.h"

// NOTE: In this test, we are actually just unit testing the ControllerImpl class.

namespace camera {

namespace {
constexpr uint32_t kDebugConfig = 0;
constexpr uint32_t kMonitorConfig = 1;
constexpr uint32_t kVideoConfig = 2;
constexpr auto kStreamTypeFR = fuchsia::camera2::CameraStreamType::FULL_RESOLUTION;
constexpr auto kStreamTypeDS = fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION;
constexpr auto kStreamTypeML = fuchsia::camera2::CameraStreamType::MACHINE_LEARNING;
constexpr auto kStreamTypeVideo = fuchsia::camera2::CameraStreamType::VIDEO_CONFERENCE;
constexpr auto kStreamTypeMonitoring = fuchsia::camera2::CameraStreamType::MONITORING;
constexpr auto kNumBuffers = 5;

class ControllerProtocolTest : public gtest::TestLoopFixture {
 public:
  ControllerProtocolTest()
      : loop_(&kAsyncLoopConfigNoAttachToCurrentThread),
        context_(sys::ComponentContext::Create()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator1_.NewRequest()));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator2_.NewRequest()));
    ASSERT_EQ(ZX_OK, loop_.StartThread("test-controller-frame-processing-thread",
                                       &controller_frame_processing_thread_));
    isp_ = fake_isp_.client();
    gdc_ = fake_gdc_.client();
    pipeline_manager_ = std::make_unique<PipelineManager>(
        fake_ddk::kFakeParent, loop_.dispatcher(), isp_, gdc_, std::move(sysmem_allocator1_));
    internal_config_info_ = SherlockInternalConfigs();
  }

  void TearDown() override {
    context_ = nullptr;
    sysmem_allocator1_ = nullptr;
    sysmem_allocator2_ = nullptr;
  }

  InternalConfigNode* GetStreamConfigNode(uint32_t config_type,
                                          const fuchsia::camera2::CameraStreamType stream_type) {
    InternalConfigInfo& config_info = internal_config_info_.configs_info.at(0);

    switch (config_type) {
      case kDebugConfig: {
        config_info = internal_config_info_.configs_info.at(0);
        break;
      }
      case kMonitorConfig: {
        config_info = internal_config_info_.configs_info.at(1);
        break;
      }
      case kVideoConfig: {
        config_info = internal_config_info_.configs_info.at(2);
        break;
      }
      default: {
        return nullptr;
      }
    }

    for (auto& stream_info : config_info.streams_info) {
      auto supported_streams = stream_info.supported_streams;
      if (std::find(supported_streams.begin(), supported_streams.end(), stream_type) !=
          supported_streams.end()) {
        return &stream_info;
      }
    }
    return nullptr;
  }

  // This helper API does the basic validation of an Input Node.
  fit::result<std::unique_ptr<camera::InputNode>, zx_status_t> GetInputNode(
      const ControllerMemoryAllocator& allocator,
      const fuchsia::camera2::CameraStreamType stream_type, StreamCreationData* info) {
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    EXPECT_NE(nullptr, info);

    info->output_buffers = std::move(buffer_collection);
    info->image_format_index = 0;

    auto result = camera::InputNode::CreateInputNode(info, allocator, loop_.dispatcher(), isp_);
    EXPECT_TRUE(result.is_ok());

    EXPECT_NE(nullptr, result.value()->isp_stream_protocol());
    EXPECT_EQ(NodeType::kInputStream, result.value()->type());
    return result;
  }

  // Returns |true| if all |streams| are present in the
  // vector |streams_to_validate|.
  bool HasAllStreams(const std::vector<fuchsia::camera2::CameraStreamType>& streams_to_validate,
                     const std::vector<fuchsia::camera2::CameraStreamType>& streams) {
    if (streams_to_validate.size() != streams.size()) {
      return false;
    }
    for (auto stream : streams) {
      if (!camera::HasStreamType(streams_to_validate, stream)) {
        return false;
      }
    }
    return true;
  }

  void TestDebugStreamConfigNode() {
    EXPECT_NE(nullptr, GetStreamConfigNode(kDebugConfig, kStreamTypeFR));
    EXPECT_EQ(nullptr, GetStreamConfigNode(kDebugConfig, kStreamTypeDS));
  }

  void TestOutputNode() {
    auto stream_type = kStreamTypeFR;
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    ControllerMemoryAllocator allocator(std::move(sysmem_allocator2_));

    // Testing successful creation of |OutputNode|.
    auto input_result = GetInputNode(allocator, stream_type, &info);
    auto output_result = OutputNode::CreateOutputNode(loop_.dispatcher(), &info,
                                                      input_result.value().get(), info.node);
    EXPECT_TRUE(output_result.is_ok());
    ASSERT_NE(nullptr, output_result.value());
    EXPECT_NE(nullptr, output_result.value()->client_stream());
    EXPECT_EQ(NodeType::kOutputStream, output_result.value()->type());

    // Passing invalid arguments.
    output_result =
        OutputNode::CreateOutputNode(nullptr, &info, input_result.value().get(), info.node);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());

    output_result = OutputNode::CreateOutputNode(loop_.dispatcher(), nullptr,
                                                 input_result.value().get(), info.node);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());

    output_result = OutputNode::CreateOutputNode(loop_.dispatcher(), &info, nullptr, info.node);
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());
  }

  void TestGdcNode() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeDS | kStreamTypeML);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    ControllerMemoryAllocator allocator(std::move(sysmem_allocator2_));

    auto input_result = GetInputNode(allocator, kStreamTypeDS | kStreamTypeML, &info);
    // Testing successful creation of |GdcNode|.
    auto next_node_internal = GetNextNodeInPipeline(kStreamTypeDS | kStreamTypeML, info.node);
    ASSERT_NE(nullptr, next_node_internal);
    auto gdc_result =
        GdcNode::CreateGdcNode(allocator, loop_.dispatcher(), fake_ddk::kFakeParent, gdc_, &info,
                               input_result.value().get(), *next_node_internal);
    EXPECT_TRUE(gdc_result.is_ok());
    ASSERT_NE(nullptr, gdc_result.value());
    EXPECT_EQ(NodeType::kGdc, gdc_result.value()->type());
  }

  void TestConfigureDebugConfig() {
    auto stream_type = kStreamTypeFR;
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    EXPECT_EQ(fr_head_node->type(), NodeType::kInputStream);
    EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(fr_head_node->supported_streams(), {stream_type}));

    auto output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());
    EXPECT_EQ(output_node->type(), NodeType::kOutputStream);
    EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(output_node->supported_streams(), {stream_type}));
    EXPECT_NE(nullptr, output_node->client_stream());
  }

  void TestConfigureMonitorConfigStreamFR() {
    auto stream_type1 = kStreamTypeDS | kStreamTypeML;
    auto stream_type2 = kStreamTypeFR | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type2);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type2);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());

    // Check if all nodes were created.
    EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
    EXPECT_EQ(NodeType::kOutputStream, output_node->type());

    // Validate the configured streams for all nodes.
    EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type2}));
    EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type2}));

    EXPECT_TRUE(HasAllStreams(fr_head_node->supported_streams(), {stream_type1, stream_type2}));

    // Check if client_stream is valid.
    EXPECT_NE(nullptr, output_node->client_stream());
  }

  void TestConfigureMonitorConfigStreamDS() {
    auto stream_type1 = kStreamTypeDS | kStreamTypeML;
    auto stream_type2 = kStreamTypeFR | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type1);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type1);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
    auto output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

    // Check if all nodes were created.
    EXPECT_EQ(NodeType::kGdc, gdc_node->type());
    EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
    EXPECT_EQ(NodeType::kOutputStream, output_node->type());

    // Validate the configured streams for all nodes.
    EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type1}));
    EXPECT_TRUE(HasAllStreams(gdc_node->configured_streams(), {stream_type1}));
    EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type1}));

    EXPECT_TRUE(HasAllStreams(fr_head_node->supported_streams(), {stream_type1, stream_type2}));
    EXPECT_TRUE(HasAllStreams(gdc_node->supported_streams(), {stream_type1}));
    EXPECT_TRUE(HasAllStreams(output_node->supported_streams(), {stream_type1}));

    // Check if client_stream is valid.
    EXPECT_NE(nullptr, output_node->client_stream());
  }

  void TestConfigure_MonitorConfig_MultiStreamFR() {
    auto stream_type1 = kStreamTypeDS | kStreamTypeML;
    auto stream_type2 = kStreamTypeFR | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type2);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type2);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    // Change the requested stream type.
    stream_config.properties.set_stream_type(stream_type1);
    info.stream_config = &stream_config;

    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto fr_ml_output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());
    auto gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(1).get());
    auto ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

    // Validate input node.
    EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type1, stream_type2}));
    EXPECT_TRUE(HasAllStreams(fr_head_node->supported_streams(), {stream_type1, stream_type2}));

    // Check if client_stream is valid.
    ASSERT_NE(nullptr, fr_ml_output_node->client_stream());
    ASSERT_NE(nullptr, ds_ml_output_node->client_stream());

    // Start streaming on FR|ML stream. Expecting other stream to be disabled.
    fr_ml_output_node->client_stream()->Start();
    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_TRUE(fr_ml_output_node->enabled());
    EXPECT_FALSE(gdc_node->enabled());
    EXPECT_FALSE(ds_ml_output_node->enabled());

    // Start streaming on DS|ML stream.
    ds_ml_output_node->client_stream()->Start();
    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_TRUE(fr_ml_output_node->enabled());
    EXPECT_TRUE(gdc_node->enabled());
    EXPECT_TRUE(ds_ml_output_node->enabled());

    // Stop streaming on FR|ML stream.
    fr_ml_output_node->client_stream()->Stop();
    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_FALSE(fr_ml_output_node->enabled());
    EXPECT_TRUE(gdc_node->enabled());
    EXPECT_TRUE(ds_ml_output_node->enabled());

    // Stop streaming on DS|ML stream.
    ds_ml_output_node->client_stream()->Stop();
    EXPECT_FALSE(fr_head_node->enabled());
    EXPECT_FALSE(fr_ml_output_node->enabled());
    EXPECT_FALSE(gdc_node->enabled());
    EXPECT_FALSE(ds_ml_output_node->enabled());
  }

  void TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering() {
    auto stream_type1 = kStreamTypeDS | kStreamTypeML;
    auto stream_type2 = kStreamTypeFR | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type1);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type1);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    // Change the requested stream type.
    stream_config.properties.set_stream_type(stream_type2);
    info.stream_config = &stream_config;

    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED, pipeline_manager_->ConfigureStreamPipeline(&info, stream));
  }

  void TestConfigureVideoConfigStream1() {
    auto stream_type = kStreamTypeFR | kStreamTypeML | kStreamTypeVideo;
    auto stream_config_node = GetStreamConfigNode(kVideoConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto gdc1_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
    auto gdc2_node = static_cast<GdcNode*>(gdc1_node->child_nodes().at(0).get());
    auto output_node = static_cast<OutputNode*>(gdc2_node->child_nodes().at(0).get());

    // Check if all nodes were created appropriately.
    EXPECT_EQ(NodeType::kGdc, gdc1_node->type());
    EXPECT_EQ(NodeType::kGdc, gdc2_node->type());
    EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
    EXPECT_EQ(NodeType::kOutputStream, output_node->type());

    // Validate the configured streams for all nodes.
    EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(gdc1_node->configured_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(gdc2_node->configured_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type}));

    EXPECT_TRUE(HasAllStreams(fr_head_node->supported_streams(), {stream_type, kStreamTypeVideo}));
    EXPECT_TRUE(HasAllStreams(gdc1_node->supported_streams(), {stream_type, kStreamTypeVideo}));
    EXPECT_TRUE(HasAllStreams(gdc2_node->supported_streams(), {stream_type}));
    EXPECT_TRUE(HasAllStreams(output_node->supported_streams(), {stream_type}));

    // Check if client_stream is valid.
    EXPECT_NE(nullptr, output_node->client_stream());
  }

  void TestShutdownPathAfterStreamingOn() {
    auto stream_type_fr = kStreamTypeFR | kStreamTypeML;
    auto stream_type_ds = kStreamTypeDS | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type_fr);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type_fr);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    info.output_buffers = std::move(buffer_collection);

    fuchsia::camera2::StreamPtr stream_fr;
    auto stream_request_fr = stream_fr.NewRequest();
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream_request_fr));
    bool stream_fr_alive = true;
    stream_fr.set_error_handler([&](zx_status_t status) { stream_fr_alive = false; });

    bool frame_received_fr = false;
    stream_fr.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
      frame_received_fr = true;
    };

    // Change the requested stream type.
    stream_config.properties.set_stream_type(stream_type_ds);
    info.stream_config = &stream_config;

    fuchsia::camera2::StreamPtr stream_ds;
    auto stream_request_ds = stream_ds.NewRequest();
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream_request_ds));
    bool stream_ds_alive = true;
    stream_ds.set_error_handler([&](zx_status_t status) { stream_ds_alive = false; });

    bool frame_received_ds = false;
    stream_ds.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
      frame_received_ds = true;
    };

    // Start streaming.
    stream_fr->Start();
    stream_ds->Start();
    RunLoopUntilIdle();

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto fr_ml_output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());
    auto gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(1).get());
    auto ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_TRUE(fr_ml_output_node->enabled());
    EXPECT_TRUE(gdc_node->enabled());
    EXPECT_TRUE(ds_ml_output_node->enabled());

    // Stop FR|ML stream.
    stream_fr->Stop();
    RunLoopUntilIdle();

    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_FALSE(fr_ml_output_node->enabled());
    EXPECT_TRUE(gdc_node->enabled());
    EXPECT_TRUE(ds_ml_output_node->enabled());

    EXPECT_EQ(fr_head_node->configured_streams().size(), 2u);
    EXPECT_EQ(fr_head_node->child_nodes().size(), 2u);

    // Disconnect FR|ML stream.
    pipeline_manager_->OnClientStreamDisconnect(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
                                                stream_type_fr);
    RunLoopUntilIdle();

    EXPECT_EQ(fr_head_node->configured_streams().size(), 1u);
    EXPECT_EQ(fr_head_node->configured_streams().at(0), stream_type_ds);
    EXPECT_EQ(fr_head_node->child_nodes().size(), 1u);

    // Disconnect DS|ML stream.
    pipeline_manager_->OnClientStreamDisconnect(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION,
                                                stream_type_ds);
    RunLoopUntilIdle();
  }

  void TestGdcConfigLoading() {
    auto result = camera::LoadGdcConfiguration(fake_ddk::kFakeParent, GdcConfig::INVALID);
    EXPECT_TRUE(result.is_error());

    result = camera::LoadGdcConfiguration(fake_ddk::kFakeParent, GdcConfig::MONITORING_360p);
    EXPECT_FALSE(result.is_error());
  }

  void TestHasStreamType() {
    std::vector<fuchsia::camera2::CameraStreamType> input_vector;
    auto stream_to_find = kStreamTypeFR;

    EXPECT_FALSE(HasStreamType(input_vector, stream_to_find));

    input_vector.push_back(kStreamTypeML);
    input_vector.push_back(kStreamTypeMonitoring);

    EXPECT_FALSE(HasStreamType(input_vector, stream_to_find));

    input_vector.push_back(kStreamTypeFR);
    EXPECT_TRUE(HasStreamType(input_vector, stream_to_find));
  }

  void TestGetNextNodeInPipeline() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeDS | kStreamTypeML);
    ASSERT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    // Expecting 1st node to be input node.
    EXPECT_EQ(NodeType::kInputStream, stream_config_node->type);

    // Using ML|DS stream in Monitor configuration for test here.
    auto next_node = camera::GetNextNodeInPipeline(info.stream_config->properties.stream_type(),
                                                   *stream_config_node);
    ASSERT_NE(nullptr, next_node);

    // Expecting 2nd node to be input node.
    EXPECT_EQ(NodeType::kGdc, next_node->type);

    next_node =
        camera::GetNextNodeInPipeline(info.stream_config->properties.stream_type(), *next_node);
    ASSERT_NE(nullptr, next_node);

    // Expecting 3rd node to be input node.
    EXPECT_EQ(NodeType::kOutputStream, next_node->type);
  }

  void TestMultipleStartStreaming() {
    auto stream_type = kStreamTypeFR;
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream));

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());

    // Set streaming on.
    output_node->client_stream()->Start();

    EXPECT_NO_FATAL_FAILURE(output_node->client_stream()->Start());
  }

  void TestInUseBufferCounts() {
    auto stream_type = kStreamTypeFR | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    info.output_buffers = std::move(buffer_collection);

    fuchsia::camera2::StreamPtr stream;
    auto stream_request = stream.NewRequest();
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream_request));
    bool stream_alive = true;
    stream.set_error_handler([&](zx_status_t status) { stream_alive = false; });

    bool frame_received = false;
    stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
      frame_received = true;
    };

    auto fr_head_node = pipeline_manager_->full_resolution_stream();

    // Start streaming.
    stream->Start();

    RunLoopUntilIdle();

    // ISP is single parent for two nodes.
    // Invoke OnFrameAvailable() for the ISP node. Buffer index = 1.
    frame_available_info_t frame_info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = 1,
        .metadata =
            {
                .timestamp = static_cast<uint64_t>(zx_clock_get_monotonic()),
                .image_format_index = 0,
                .input_buffer_index = 0,
            },
    };

    EXPECT_NO_FATAL_FAILURE(fr_head_node->OnFrameAvailable(&frame_info));

    while (!frame_received) {
      RunLoopUntilIdle();
    }

    EXPECT_TRUE(frame_received);

    EXPECT_EQ(fr_head_node->get_in_use_buffer_count(0), 0u);
    EXPECT_EQ(fr_head_node->get_in_use_buffer_count(frame_info.buffer_id), 1u);

    stream->ReleaseFrame(frame_info.buffer_id);
    RunLoopUntilIdle();

    EXPECT_EQ(fr_head_node->get_in_use_buffer_count(frame_info.buffer_id), 0u);
    stream->Stop();
  }

  void TestReleaseAfterStopStreaming() {
    auto stream_type = kStreamTypeDS | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    info.output_buffers = std::move(buffer_collection);

    fuchsia::camera2::StreamPtr stream;
    auto stream_request = stream.NewRequest();
    EXPECT_EQ(ZX_OK, pipeline_manager_->ConfigureStreamPipeline(&info, stream_request));

    // Start streaming.
    stream->Start();
    RunLoopUntilIdle();

    auto fr_head_node = pipeline_manager_->full_resolution_stream();
    auto gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
    auto ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

    EXPECT_FALSE(fake_isp_.frame_released());

    EXPECT_TRUE(fr_head_node->enabled());
    EXPECT_TRUE(gdc_node->enabled());
    EXPECT_TRUE(ds_ml_output_node->enabled());

    // Stop streaming.
    stream->Stop();
    RunLoopUntilIdle();

    EXPECT_FALSE(fr_head_node->enabled());
    EXPECT_FALSE(gdc_node->enabled());
    EXPECT_FALSE(ds_ml_output_node->enabled());

    // Invoke OnFrameAvailable() for the ISP node. Buffer index = 1.
    frame_available_info_t frame_info = {
        .frame_status = FRAME_STATUS_OK,
        .buffer_id = 1,
        .metadata =
            {
                .timestamp = static_cast<uint64_t>(zx_clock_get_monotonic()),
                .image_format_index = 0,
                .input_buffer_index = 0,
            },
    };

    // Making a frame available to ISP node.
    // Expecting the frame to be released since node is disabled.
    EXPECT_NO_FATAL_FAILURE(fr_head_node->OnFrameAvailable(&frame_info));
    EXPECT_TRUE(fake_isp_.frame_released());

    // Making a frame available to GDC node.
    // Expecting the frame to be released since node is disabled.
    EXPECT_NO_FATAL_FAILURE(gdc_node->OnFrameAvailable(&frame_info));
    EXPECT_TRUE(fake_gdc_.frame_released());
  }

  FakeIsp fake_isp_;
  FakeGdc fake_gdc_;
  async::Loop loop_;
  thrd_t controller_frame_processing_thread_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<camera::PipelineManager> pipeline_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator1_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator2_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  InternalConfigs internal_config_info_;
};

TEST_F(ControllerProtocolTest, GetDebugStreamConfig) { TestDebugStreamConfigNode(); }

TEST_F(ControllerProtocolTest, ConfigureOutputNodeDebugConfig) { TestConfigureDebugConfig(); }

TEST_F(ControllerProtocolTest, TestShutdownPathAfterStreamingOn) {
  TestShutdownPathAfterStreamingOn();
}

TEST_F(ControllerProtocolTest, TestConfigureMonitorConfigStreamFR) {
  TestConfigureMonitorConfigStreamFR();
}

TEST_F(ControllerProtocolTest, TestConfigureMonitorConfigStreamDS) {
  TestConfigureMonitorConfigStreamDS();
}

TEST_F(ControllerProtocolTest, TestConfigureVideoConfigStream1) {
  TestConfigureVideoConfigStream1();
}

TEST_F(ControllerProtocolTest, TestHasStreamType) { TestHasStreamType(); }

TEST_F(ControllerProtocolTest, TestNextNodeInPipeline) { TestGetNextNodeInPipeline(); }

TEST_F(ControllerProtocolTest, TestMultipleStartStreaming) { TestMultipleStartStreaming(); }

TEST_F(ControllerProtocolTest, TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering) {
  TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering();
}

TEST_F(ControllerProtocolTest, TestConfigure_MonitorConfig_MultiStreamFR) {
  TestConfigure_MonitorConfig_MultiStreamFR();
}

TEST_F(ControllerProtocolTest, TestInUseBufferCounts) { TestInUseBufferCounts(); }

TEST_F(ControllerProtocolTest, TestOutputNode) { TestOutputNode(); }

TEST_F(ControllerProtocolTest, TestGdcNode) { TestGdcNode(); }

TEST_F(ControllerProtocolTest, TestReleaseAfterStopStreaming) { TestReleaseAfterStopStreaming(); }

TEST_F(ControllerProtocolTest, LoadGdcConfig) {
#ifdef INTERNAL_ACCESS
  TestGdcConfigLoading();
#else
  GTEST_SKIP();
#endif
}
}  // namespace

}  // namespace camera
