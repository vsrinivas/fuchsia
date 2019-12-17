// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/controller-protocol.h"

#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/function.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/syscalls.h>

#include <fbl/auto_call.h>

#include "fake_gdc.h"
#include "fake_isp.h"
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
    ASSERT_EQ(ZX_OK, loop_.StartThread("camera-controller-loop"));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator1_.NewRequest()));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator2_.NewRequest()));
    isp_ = fake_isp_.client();
    gdc_ = fake_gdc_.client();
    controller_protocol_device_ = std::make_unique<ControllerImpl>(
        fake_ddk::kFakeParent, isp_, gdc_, std::move(sysmem_allocator1_));
    pipeline_manager_ = std::make_unique<PipelineManager>(fake_ddk::kFakeParent, isp_, gdc_,
                                                          std::move(sysmem_allocator2_));
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
    EXPECT_EQ(info->streams_info[0].input_stream_type, kStreamTypeFR);
    // FR supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[0].supported_streams[0], kStreamTypeFR);

    // Monitor Configuration
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(kMonitorConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 2u);

    // 1st Stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type, kStreamTypeFR);

    // FR Supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 2u);
    EXPECT_EQ(info->streams_info[0].supported_streams[0], kStreamTypeFR | kStreamTypeML);
    EXPECT_EQ(info->streams_info[0].supported_streams[1], kStreamTypeDS | kStreamTypeML);

    // 2nd Stream is DS
    EXPECT_EQ(info->streams_info[1].input_stream_type, kStreamTypeDS);

    // DS supported streams
    EXPECT_EQ(info->streams_info[1].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[1].supported_streams[0], kStreamTypeMonitoring);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].type, kGdc);
    ASSERT_EQ(info->streams_info[1].child_nodes[0].gdc_info.config_type.size(), 3u);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].gdc_info.config_type[0],
              GdcConfig::MONITORING_720p);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].gdc_info.config_type[1],
              GdcConfig::MONITORING_480p);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].gdc_info.config_type[2],
              GdcConfig::MONITORING_360p);
    ASSERT_EQ(info->streams_info[1].child_nodes[0].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].supported_streams[0], kStreamTypeMonitoring);

    // Output node.
    EXPECT_EQ(info->streams_info[1].child_nodes[0].child_nodes.size(), 1u);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].child_nodes[0].type, kOutputStream);
    ASSERT_EQ(info->streams_info[1].child_nodes[0].child_nodes[0].supported_streams.size(), 1u);
    EXPECT_EQ(info->streams_info[1].child_nodes[0].child_nodes[0].supported_streams[0],
              kStreamTypeMonitoring);

    // Video Conferencing configuration
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(kVideoConfig, &info));
    EXPECT_EQ(info->streams_info.size(), 1u);

    // 1st stream is FR
    EXPECT_EQ(info->streams_info[0].input_stream_type, kStreamTypeFR);
    // FR Supported streams
    EXPECT_EQ(info->streams_info[0].supported_streams.size(), 2u);
    EXPECT_EQ(info->streams_info[0].supported_streams[0],
              kStreamTypeFR | kStreamTypeML | kStreamTypeVideo);
    EXPECT_EQ(info->streams_info[0].supported_streams[1], kStreamTypeVideo);
  }

  InternalConfigNode* GetStreamConfigNode(uint32_t config_type,
                                          const fuchsia::camera2::CameraStreamType stream_type) {
    controller_protocol_device_->PopulateConfigurations();
    InternalConfigInfo* info = nullptr;
    // Get internal configuration for debug config.
    EXPECT_EQ(ZX_OK, controller_protocol_device_->GetInternalConfiguration(config_type, &info));
    // Get stream config for kStreamTypeFR; stream.
    return controller_protocol_device_->GetStreamConfigNode(info, stream_type);
  }

  // This helper API does the basic validation of an Input Node.
  fit::result<std::unique_ptr<camera::ProcessNode>, zx_status_t> GetInputNode(
      const fuchsia::camera2::CameraStreamType stream_type, StreamCreationData* info) {
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    EXPECT_NE(nullptr, info);

    info->output_buffers = std::move(buffer_collection);
    info->image_format_index = 0;

    auto result = pipeline_manager_->CreateInputNode(info);
    EXPECT_TRUE(result.is_ok());

    EXPECT_NE(nullptr, result.value()->isp_stream_protocol());
    EXPECT_EQ(NodeType::kInputStream, result.value()->type());
    return result;
  }

  // This helper API does the basic validation of an Output Node.
  fit::result<camera::ProcessNode*, zx_status_t> GetGraphNode(StreamCreationData* info,
                                                              ProcessNode* input_node) {
    auto graph_result = pipeline_manager_->CreateGraph(info, info->node, input_node);
    EXPECT_TRUE(graph_result.is_ok());

    EXPECT_NE(nullptr, graph_result.value());
    EXPECT_NE(nullptr, graph_result.value()->client_stream());
    EXPECT_EQ(NodeType::kOutputStream, graph_result.value()->type());
    return graph_result;
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

  void TestConfigureDebugConfig() {
    auto stream_type = kStreamTypeFR;
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    __UNUSED auto result = GetInputNode(stream_type, &info);
    __UNUSED auto graph_result = GetGraphNode(&info, result.value().get());
  }

  void TestConfigureMonitorConfigStreamFR() {
    auto stream_type = kStreamTypeDS | kStreamTypeML;
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, stream_type);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(stream_type);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    auto result = GetInputNode(stream_type, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Check if GDC node was created.
    EXPECT_EQ(NodeType::kGdc, graph_result.value()->parent_node()->type());

    // Validate the configured and supported streams for Input node.
    EXPECT_TRUE(HasAllStreams(result.value()->configured_streams(), {stream_type}));

    EXPECT_TRUE(HasAllStreams(result.value()->supported_streams(),
                              {stream_type, kStreamTypeFR | kStreamTypeML}));

    // Validate the configured and supported streams for GDC node.
    EXPECT_TRUE(
        HasAllStreams(graph_result.value()->parent_node()->configured_streams(), {stream_type}));

    EXPECT_TRUE(
        HasAllStreams(graph_result.value()->parent_node()->supported_streams(), {stream_type}));

    // Validate the configured and supported streams for Output node.
    EXPECT_TRUE(HasAllStreams(graph_result.value()->configured_streams(), {stream_type}));

    EXPECT_TRUE(HasAllStreams(graph_result.value()->supported_streams(), {stream_type}));

    // Check if the stream got created.
    EXPECT_TRUE(pipeline_manager_->IsStreamAlreadyCreated(&info, result.value().get()));

    // Change the requested stream type.
    stream_config.properties.set_stream_type(kStreamTypeFR | kStreamTypeML);
    info.stream_config = &stream_config;

    auto append_result =
        pipeline_manager_->FindNodeToAttachNewStream(&info, info.node, result.value().get());
    ASSERT_EQ(true, append_result.is_ok());

    EXPECT_EQ(NodeType::kInputStream, append_result.value().second->type());
    EXPECT_EQ(append_result.value().second->supported_streams().size(), 2u);

    // Check for a stream which is not created.
    EXPECT_FALSE(pipeline_manager_->IsStreamAlreadyCreated(&info, result.value().get()));

    // Change the requested stream type to something invalid for this configuration.
    stream_config.properties.set_stream_type(kStreamTypeML);
    info.stream_config = &stream_config;

    append_result =
        pipeline_manager_->FindNodeToAttachNewStream(&info, info.node, result.value().get());
    ASSERT_EQ(true, append_result.is_error());
    EXPECT_EQ(ZX_ERR_INVALID_ARGS, append_result.error());
  }

  void TestConfigureMonitorConfigStreamDS() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeMonitoring);
    ASSERT_NE(nullptr, stream_config_node);
    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeMonitoring);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    auto result = GetInputNode(kStreamTypeMonitoring, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Check if GDC node was created.
    EXPECT_EQ(NodeType::kGdc, graph_result.value()->parent_node()->type());

    // Validate the configured and supported streams for Input node.
    EXPECT_TRUE(HasAllStreams(result.value()->configured_streams(), {kStreamTypeMonitoring}));
    EXPECT_TRUE(HasAllStreams(result.value()->supported_streams(), {kStreamTypeMonitoring}));

    // Validate the configured and supported streams for GDC node.
    EXPECT_TRUE(HasAllStreams(graph_result.value()->parent_node()->supported_streams(),
                              {kStreamTypeMonitoring}));
    EXPECT_TRUE(HasAllStreams(graph_result.value()->parent_node()->configured_streams(),
                              {kStreamTypeMonitoring}));

    // Validate the configured and supported streams for Output node.
    EXPECT_TRUE(HasAllStreams(graph_result.value()->supported_streams(), {kStreamTypeMonitoring}));
    EXPECT_TRUE(HasAllStreams(graph_result.value()->configured_streams(), {kStreamTypeMonitoring}));
  }

  void TestConfigureVideoConfigStream1() {
    auto stream_config_node =
        GetStreamConfigNode(kVideoConfig, kStreamTypeFR | kStreamTypeML | kStreamTypeVideo);
    ASSERT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeFR | kStreamTypeML | kStreamTypeVideo);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    auto result = GetInputNode(kStreamTypeFR | kStreamTypeML | kStreamTypeVideo, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Check if GDC1 & GDC2 node was created.
    EXPECT_EQ(NodeType::kGdc, graph_result.value()->parent_node()->type());
    EXPECT_EQ(NodeType::kGdc, graph_result.value()->parent_node()->parent_node()->type());

    // Validate the configured and supported streams for Input node.
    EXPECT_TRUE(HasAllStreams(result.value()->configured_streams(),
                              {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));
    EXPECT_TRUE(
        HasAllStreams(result.value()->supported_streams(),
                      {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo, kStreamTypeVideo}));

    // Validate the configured and supported streams for GDC2 node.
    EXPECT_TRUE(HasAllStreams(graph_result.value()->parent_node()->configured_streams(),
                              {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));
    EXPECT_TRUE(HasAllStreams(graph_result.value()->parent_node()->supported_streams(),
                              {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));

    // Validate the configured and supported streams for GDC1 node.
    EXPECT_TRUE(
        HasAllStreams(graph_result.value()->parent_node()->parent_node()->configured_streams(),
                      {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));
    EXPECT_TRUE(
        HasAllStreams(graph_result.value()->parent_node()->parent_node()->supported_streams(),
                      {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo, kStreamTypeVideo}));

    // Validate the configured and supported streams for Output node.
    EXPECT_TRUE(HasAllStreams(graph_result.value()->configured_streams(),
                              {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));
    EXPECT_TRUE(HasAllStreams(graph_result.value()->supported_streams(),
                              {kStreamTypeFR | kStreamTypeML | kStreamTypeVideo}));

    // Check if the stream got created.
    EXPECT_TRUE(pipeline_manager_->IsStreamAlreadyCreated(&info, result.value().get()));

    // Change the requested stream type.
    stream_config.properties.set_stream_type(kStreamTypeVideo);
    info.stream_config = &stream_config;

    auto append_result =
        pipeline_manager_->FindNodeToAttachNewStream(&info, info.node, result.value().get());
    ASSERT_EQ(true, append_result.is_ok());

    EXPECT_EQ(NodeType::kGdc, append_result.value().second->type());
    EXPECT_EQ(append_result.value().second->supported_streams().size(), 2u);

    // Check for a stream which is not created.
    EXPECT_FALSE(pipeline_manager_->IsStreamAlreadyCreated(&info, result.value().get()));
  }

  void TestShutdownPathAfterStreamingOn() {
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, kStreamTypeFR);
    ASSERT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeFR);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    auto result = GetInputNode(kStreamTypeFR, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Set streaming on.
    graph_result.value()->client_stream()->Start();

    EXPECT_NO_FATAL_FAILURE(pipeline_manager_->OnClientStreamDisconnect(&info));
  }

  void TestGdcConfigLoading() {
    auto result = pipeline_manager_->LoadGdcConfiguration(GdcConfig::INVALID);
    EXPECT_TRUE(result.is_error());

    result = pipeline_manager_->LoadGdcConfiguration(GdcConfig::MONITORING_360p);
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

  void TestMultipleStartStreaming() {
    auto stream_config_node = GetStreamConfigNode(kDebugConfig, kStreamTypeFR);
    ASSERT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeFR);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;

    auto result = GetInputNode(kStreamTypeFR, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Set streaming on.
    graph_result.value()->client_stream()->Start();

    EXPECT_NO_FATAL_FAILURE(graph_result.value()->client_stream()->Start());
  }

  void TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeDS | kStreamTypeML);
    ASSERT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    auto result = GetInputNode(kStreamTypeDS | kStreamTypeML, &info);
    __UNUSED auto graph_result = GetGraphNode(&info, result.value().get());

    // Change the requested stream type.
    stream_config.properties.set_stream_type(kStreamTypeFR | kStreamTypeML);
    info.stream_config = &stream_config;
    fidl::InterfaceRequest<fuchsia::camera2::Stream> stream;

    EXPECT_EQ(ZX_ERR_NOT_SUPPORTED,
              pipeline_manager_->AppendToExistingGraph(&info, result.value().get(), stream));
  }

  void TestConfigure_MonitorConfig_MultiStreamFR() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeFR | kStreamTypeML);
    EXPECT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeFR | kStreamTypeML);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    auto result = GetInputNode(kStreamTypeFR | kStreamTypeML, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Change the requested stream type.
    stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
    info.stream_config = &stream_config;

    auto append_result =
        pipeline_manager_->FindNodeToAttachNewStream(&info, info.node, result.value().get());
    EXPECT_EQ(true, append_result.is_ok());

    auto output_node_result = pipeline_manager_->CreateGraph(&info, append_result.value().first,
                                                             append_result.value().second);
    EXPECT_EQ(true, output_node_result.is_ok());

    // Push this new requested stream to all pre-existing nodes |configured_streams| vector.
    auto requested_stream_type = info.stream_config->properties.stream_type();
    auto current_node = append_result.value().second;
    while (current_node) {
      current_node->configured_streams().push_back(requested_stream_type);
      current_node = current_node->parent_node();
    }

    auto ml_fr_output = graph_result.value();
    auto ml_ds_output = output_node_result.value();

    EXPECT_EQ(ml_ds_output->type(), NodeType::kOutputStream);
    EXPECT_TRUE(HasAllStreams(ml_ds_output->supported_streams(), {kStreamTypeDS | kStreamTypeML}));

    EXPECT_EQ(ml_ds_output->parent_node()->type(), NodeType::kGdc);
    EXPECT_TRUE(HasAllStreams(ml_ds_output->parent_node()->supported_streams(),
                              {kStreamTypeDS | kStreamTypeML}));

    EXPECT_EQ(ml_ds_output->parent_node()->parent_node()->type(), NodeType::kInputStream);
    EXPECT_TRUE(HasAllStreams(ml_ds_output->parent_node()->parent_node()->supported_streams(),
                              {kStreamTypeDS | kStreamTypeML, kStreamTypeFR | kStreamTypeML}));
    EXPECT_TRUE(HasAllStreams(ml_ds_output->parent_node()->parent_node()->configured_streams(),
                              {kStreamTypeDS | kStreamTypeML, kStreamTypeFR | kStreamTypeML}));

    // Test Streaming On for ML FR stream.
    ml_fr_output->client_stream()->Start();
    EXPECT_TRUE(ml_fr_output->enabled());
    EXPECT_TRUE(ml_fr_output->parent_node()->enabled());
    // Expect the ML DS stream to be disabled.
    EXPECT_FALSE(ml_ds_output->enabled());
    EXPECT_FALSE(ml_ds_output->parent_node()->enabled());
    EXPECT_TRUE(ml_ds_output->parent_node()->parent_node()->enabled());

    // Now test the streaming on for ML DS stream.
    ml_ds_output->client_stream()->Start();
    EXPECT_TRUE(ml_ds_output->enabled());
    EXPECT_TRUE(ml_ds_output->parent_node()->enabled());
    EXPECT_TRUE(ml_ds_output->parent_node()->parent_node()->enabled());
    // ML FR should still be streaming.
    EXPECT_TRUE(ml_fr_output->enabled());
    EXPECT_TRUE(ml_fr_output->parent_node()->enabled());

    // Stop ML FR stream.
    ml_fr_output->client_stream()->Stop();
    EXPECT_FALSE(ml_fr_output->enabled());
    EXPECT_TRUE(ml_fr_output->parent_node()->enabled());
    // Expect the ML DS stream to be enabled.
    EXPECT_TRUE(ml_ds_output->enabled());
    EXPECT_TRUE(ml_ds_output->parent_node()->enabled());
    EXPECT_TRUE(ml_ds_output->parent_node()->parent_node()->enabled());

    // Stop ML DS stream.
    ml_ds_output->client_stream()->Stop();
    EXPECT_FALSE(ml_ds_output->enabled());
    EXPECT_FALSE(ml_ds_output->parent_node()->enabled());
    EXPECT_FALSE(ml_ds_output->parent_node()->parent_node()->enabled());
    // ML FR should be disabled.
    EXPECT_FALSE(ml_fr_output->enabled());
    EXPECT_FALSE(ml_fr_output->parent_node()->enabled());
  }

  void TestInUseBufferCounts() {
    auto stream_config_node = GetStreamConfigNode(kMonitorConfig, kStreamTypeFR | kStreamTypeML);
    EXPECT_NE(nullptr, stream_config_node);

    StreamCreationData info;
    fuchsia::camera2::hal::StreamConfig stream_config;
    stream_config.properties.set_stream_type(kStreamTypeFR | kStreamTypeML);
    info.stream_config = &stream_config;
    info.node = *stream_config_node;
    auto result = GetInputNode(kStreamTypeFR | kStreamTypeML, &info);
    auto graph_result = GetGraphNode(&info, result.value().get());

    // Change the requested stream type.
    stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
    info.stream_config = &stream_config;

    auto append_result =
        pipeline_manager_->FindNodeToAttachNewStream(&info, info.node, result.value().get());
    EXPECT_EQ(true, append_result.is_ok());

    auto output_node_result = pipeline_manager_->CreateGraph(&info, append_result.value().first,
                                                             append_result.value().second);
    EXPECT_EQ(true, output_node_result.is_ok());

    // Push this new requested stream to all pre-existing nodes |configured_streams| vector.
    auto requested_stream_type = info.stream_config->properties.stream_type();
    auto current_node = append_result.value().second;
    while (current_node) {
      current_node->configured_streams().push_back(requested_stream_type);
      current_node = current_node->parent_node();
    }

    auto ml_fr_output = graph_result.value();
    auto ml_ds_output = output_node_result.value();
    auto isp_node = ml_fr_output->parent_node();

    auto isp_stream_protocol = std::make_unique<camera::IspStreamProtocol>();
    fake_isp_.PopulateStreamProtocol(isp_stream_protocol->protocol());
    isp_node->set_isp_stream_protocol(std::move(isp_stream_protocol));

    // Start streaming both streams.
    ml_fr_output->client_stream()->Start();
    ml_ds_output->client_stream()->Start();

    // Disable the output node so we do not client.
    // This is needed for tests.
    ml_fr_output->set_enabled(false);
    ml_ds_output->set_enabled(false);

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

    EXPECT_NO_FATAL_FAILURE(isp_node->OnFrameAvailable(&frame_info));

    EXPECT_EQ(isp_node->get_in_use_buffer_count(0), 0u);
    EXPECT_EQ(isp_node->get_in_use_buffer_count(frame_info.buffer_id), 1u);

    EXPECT_NO_FATAL_FAILURE(isp_node->OnReleaseFrame(frame_info.buffer_id));
    EXPECT_EQ(isp_node->get_in_use_buffer_count(frame_info.buffer_id), 0u);

    ml_fr_output->client_stream()->Stop();
    ml_ds_output->client_stream()->Stop();
  }

  FakeIsp fake_isp_;
  FakeGdc fake_gdc_;
  async::Loop loop_;
  std::unique_ptr<ControllerImpl> controller_protocol_device_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<camera::PipelineManager> pipeline_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator1_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator2_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
};

TEST_F(ControllerProtocolTest, GetConfigs) { TestInternalConfigs(); }

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

TEST_F(ControllerProtocolTest, TestMultipleStartStreaming) { TestMultipleStartStreaming(); }

TEST_F(ControllerProtocolTest, TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering) {
  TestConfigure_MonitorConfig_MultiStreamFR_BadOrdering();
}

TEST_F(ControllerProtocolTest, TestConfigure_MonitorConfig_MultiStreamFR) {
  TestConfigure_MonitorConfig_MultiStreamFR();
}

TEST_F(ControllerProtocolTest, TestInUseBufferCounts) { TestInUseBufferCounts(); }

TEST_F(ControllerProtocolTest, LoadGdcConfig) {
#ifdef INTERNAL_ACCESS
  TestGdcConfigLoading();
#else
  GTEST_SKIP();
#endif
}
}  // namespace

}  // namespace camera
