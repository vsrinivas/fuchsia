// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/async/cpp/task.h>
#include <lib/fake_ddk/fake_ddk.h>
#include <lib/fit/result.h>
#include <lib/gtest/test_loop_fixture.h>
#include <lib/sys/cpp/component_context.h>
#include <zircon/errors.h>
#include <zircon/syscalls.h>
#include <zircon/types.h>

#include <utility>

#include "src/camera/drivers/controller/configs/sherlock/sherlock_configs.h"
#include "src/camera/drivers/controller/controller_protocol.h"
#include "src/camera/drivers/controller/gdc_node.h"
#include "src/camera/drivers/controller/ge2d_node.h"
#include "src/camera/drivers/controller/graph_utils.h"
#include "src/camera/drivers/controller/input_node.h"
#include "src/camera/drivers/controller/isp_stream_protocol.h"
#include "src/camera/drivers/controller/output_node.h"
#include "src/camera/drivers/controller/pipeline_manager.h"
#include "src/camera/drivers/controller/test/constants.h"
#include "src/camera/drivers/controller/test/fake_gdc.h"
#include "src/camera/drivers/controller/test/fake_ge2d.h"
#include "src/camera/drivers/controller/test/fake_isp.h"

// NOTE: In this test, we are actually just unit testing the ControllerImpl class.
namespace camera {

namespace {

class ControllerProtocolTest : public gtest::TestLoopFixture {
 public:
  ControllerProtocolTest() : context_(sys::ComponentContext::CreateAndServeOutgoingDirectory()) {}

  void SetUp() override {
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator1_.NewRequest()));
    ASSERT_EQ(ZX_OK, context_->svc()->Connect(sysmem_allocator2_.NewRequest()));
    ASSERT_EQ(ZX_OK, zx::event::create(0, &event_));

    isp_ = fake_isp_.client();
    gdc_ = fake_gdc_.client();
    ge2d_ = fake_ge2d_.client();
    pipeline_manager_ =
        std::make_unique<PipelineManager>(fake_ddk::kFakeParent, dispatcher(), isp_, gdc_, ge2d_,
                                          std::move(sysmem_allocator1_), event_);
    internal_config_info_ = SherlockInternalConfigs();
  }

  void TearDown() override {
    pipeline_manager_ = nullptr;
    QuitLoop();
    context_ = nullptr;
    sysmem_allocator1_ = nullptr;
    sysmem_allocator2_ = nullptr;
  }

  InternalConfigNode* GetStreamConfigNode(uint32_t config_type,
                                          const fuchsia::camera2::CameraStreamType stream_type) {
    InternalConfigInfo& config_info = internal_config_info_.configs_info.at(0);

    if (config_type >= SherlockConfigs::MAX) {
      return nullptr;
    }

    config_info = internal_config_info_.configs_info.at(config_type);

    for (auto& stream_info : config_info.streams_info) {
      auto supported_streams = stream_info.supported_streams;
      if (std::any_of(supported_streams.begin(), supported_streams.end(),
                      [stream_type](auto& supported_stream) {
                        return supported_stream.type == stream_type;
                      })) {
        return &stream_info;
      }
    }
    return nullptr;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 FakeBufferCollection() {
    fuchsia::sysmem::BufferCollectionInfo_2 buffer_collection;
    buffer_collection.buffer_count = kNumBuffers;
    buffer_collection.settings.has_image_format_constraints = true;
    auto& constraints = buffer_collection.settings.image_format_constraints;
    constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    constraints.pixel_format.type = fuchsia::sysmem::PixelFormatType::NV12;
    constraints.max_coded_width = 4096;
    constraints.max_coded_height = 4096;
    constraints.max_bytes_per_row = 0xffffffff;
    return buffer_collection;
  }

  // This helper API does the basic validation of an Input Node.
  fit::result<std::unique_ptr<camera::InputNode>, zx_status_t> GetInputNode(
      const ControllerMemoryAllocator& allocator, StreamCreationData* info) {
    EXPECT_NE(nullptr, info);

    info->output_buffers = FakeBufferCollection();
    info->image_format_index = 0;

    auto result = camera::InputNode::CreateInputNode(info, allocator, dispatcher(), isp_);
    EXPECT_TRUE(result.is_ok());

    EXPECT_NE(nullptr, result.value()->isp_stream_protocol());
    EXPECT_EQ(NodeType::kInputStream, result.value()->type());
    return result;
  }

  // Returns |true| if all |streams| are present in the
  // vector |streams_to_validate|.
  bool HasAllStreams(const std::vector<fuchsia::camera2::CameraStreamType>& streams_to_validate,
                     const std::vector<fuchsia::camera2::CameraStreamType>& streams) const {
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

  zx_status_t SetupStream(uint32_t config, fuchsia::camera2::CameraStreamType stream_type,
                          fuchsia::camera2::StreamPtr& stream) {
    async::PostTask(dispatcher(), [this, config, stream_type, &stream]() {
      auto* stream_config_node = GetStreamConfigNode(config, stream_type);
      StreamCreationData info;
      fuchsia::camera2::hal::StreamConfig stream_config;
      stream_config.properties.set_stream_type(stream_type);
      info.stream_config = std::move(stream_config);
      info.node = *stream_config_node;
      info.output_buffers = FakeBufferCollection();
      info.image_format_index = 0;
      auto stream_request = stream.NewRequest();
      pipeline_manager_->ConfigureStreamPipeline(std::move(info), std::move(stream_request));
    });

    RunLoopUntilIdle();
    return ZX_OK;
  }

  std::vector<fuchsia::sysmem::ImageFormat_2> GetOutputFormats(
      const fuchsia::camera2::StreamPtr& stream) {
    bool callback_called = false;
    std::vector<fuchsia::sysmem::ImageFormat_2> output_formats;
    stream->GetImageFormats([&](std::vector<fuchsia::sysmem::ImageFormat_2> formats) {
      callback_called = true;
      output_formats = std::move(formats);
    });
    RunLoopUntilIdle();
    EXPECT_TRUE(callback_called);
    return output_formats;
  }

  FakeIsp fake_isp_;
  FakeGdc fake_gdc_;
  FakeGe2d fake_ge2d_;
  zx::event event_;
  thrd_t controller_frame_processing_thread_;
  fuchsia::camera2::hal::ControllerSyncPtr camera_client_;
  std::unique_ptr<sys::ComponentContext> context_;
  std::unique_ptr<camera::PipelineManager> pipeline_manager_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator1_;
  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator2_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
  InternalConfigs internal_config_info_;
};

TEST_F(ControllerProtocolTest, TestConfigureMonitorConfigStreamFR) {
  fuchsia::camera2::StreamPtr stream;
  auto stream_type1 = kStreamTypeDS | kStreamTypeML;
  auto stream_type2 = kStreamTypeFR | kStreamTypeML;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type2, stream));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());

  // Check if all nodes were created.
  EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
  EXPECT_EQ(NodeType::kOutputStream, output_node->type());

  // Validate the configured streams for all nodes.
  EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type2}));
  EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type2}));

  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type1));
  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type2));

  // Check if client_stream is valid.
  EXPECT_NE(nullptr, output_node->client_stream());

  auto output_formats = GetOutputFormats(stream);
  EXPECT_EQ(output_formats.size(), 1u);
}

TEST_F(ControllerProtocolTest, TestConfigureMonitorConfigStreamDS) {
  auto stream_type1 = kStreamTypeDS | kStreamTypeML;
  auto stream_type2 = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type1, stream));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
  auto* output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

  // Check if all nodes were created.
  EXPECT_EQ(NodeType::kGdc, gdc_node->type());
  EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
  EXPECT_EQ(NodeType::kOutputStream, output_node->type());

  // Validate the configured streams for all nodes.
  EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type1}));
  EXPECT_TRUE(HasAllStreams(gdc_node->configured_streams(), {stream_type1}));
  EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type1}));

  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type1));
  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type2));
  EXPECT_TRUE(gdc_node->is_stream_supported(stream_type1));
  EXPECT_TRUE(output_node->is_stream_supported(stream_type1));

  // Check if client_stream is valid.
  EXPECT_NE(nullptr, output_node->client_stream());

  auto output_formats = GetOutputFormats(stream);
  EXPECT_EQ(output_formats.size(), 1u);
}

TEST_F(ControllerProtocolTest, TestConfigureVideoConfigStream1) {
  auto stream_type = kStreamTypeFR | kStreamTypeML | kStreamTypeVideo;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::VIDEO, stream_type, stream));

  fuchsia::camera2::StreamPtr stream_video;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::VIDEO, kStreamTypeVideo, stream_video));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* gdc1_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
  auto* gdc2_node = static_cast<GdcNode*>(gdc1_node->child_nodes().at(0).get());
  auto* output_node = static_cast<OutputNode*>(gdc2_node->child_nodes().at(0).get());
  auto* ge2d_node = static_cast<Ge2dNode*>(gdc1_node->child_nodes().at(1).get());
  auto* output_node_video = static_cast<OutputNode*>(ge2d_node->child_nodes().at(0).get());

  // Check if all nodes were created appropriately.
  EXPECT_EQ(NodeType::kGdc, gdc1_node->type());
  EXPECT_EQ(NodeType::kGdc, gdc2_node->type());
  EXPECT_EQ(NodeType::kGe2d, ge2d_node->type());
  EXPECT_EQ(NodeType::kInputStream, fr_head_node->type());
  EXPECT_EQ(NodeType::kOutputStream, output_node->type());
  EXPECT_EQ(NodeType::kOutputStream, output_node_video->type());

  // Validate the configured streams for all nodes.
  EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type, kStreamTypeVideo}));
  EXPECT_TRUE(HasAllStreams(gdc1_node->configured_streams(), {stream_type, kStreamTypeVideo}));
  EXPECT_TRUE(HasAllStreams(gdc2_node->configured_streams(), {stream_type}));
  EXPECT_TRUE(HasAllStreams(ge2d_node->configured_streams(), {kStreamTypeVideo}));
  EXPECT_TRUE(HasAllStreams(output_node->configured_streams(), {stream_type}));
  EXPECT_TRUE(HasAllStreams(output_node_video->configured_streams(), {kStreamTypeVideo}));

  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type));
  EXPECT_TRUE(fr_head_node->is_stream_supported(kStreamTypeVideo));
  EXPECT_TRUE(gdc1_node->is_stream_supported(stream_type));
  EXPECT_TRUE(gdc1_node->is_stream_supported(kStreamTypeVideo));
  EXPECT_TRUE(gdc2_node->is_stream_supported(stream_type));
  EXPECT_TRUE(ge2d_node->is_stream_supported(kStreamTypeVideo));
  EXPECT_TRUE(output_node->is_stream_supported(stream_type));
  EXPECT_TRUE(output_node_video->is_stream_supported(kStreamTypeVideo));

  // Check if client_stream is valid.
  EXPECT_NE(nullptr, output_node->client_stream());
  EXPECT_NE(nullptr, output_node_video->client_stream());

  auto output_formats = GetOutputFormats(stream);
  EXPECT_EQ(output_formats.size(), 1u);

  output_formats = GetOutputFormats(stream_video);
  ASSERT_EQ(output_formats.size(), 3u);
}

TEST_F(ControllerProtocolTest, TestHasStreamType) {
  std::vector<fuchsia::camera2::CameraStreamType> input_vector;
  auto stream_to_find = kStreamTypeFR;

  EXPECT_FALSE(HasStreamType(input_vector, stream_to_find));

  input_vector.push_back(kStreamTypeML);
  input_vector.push_back(kStreamTypeMonitoring);

  EXPECT_FALSE(HasStreamType(input_vector, stream_to_find));

  input_vector.push_back(kStreamTypeFR);
  EXPECT_TRUE(HasStreamType(input_vector, stream_to_find));
}

TEST_F(ControllerProtocolTest, TestNextNodeInPipeline) {
  auto* stream_config_node =
      GetStreamConfigNode(SherlockConfigs::MONITORING, kStreamTypeDS | kStreamTypeML);
  ASSERT_NE(nullptr, stream_config_node);

  StreamCreationData info;
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
  info.stream_config = std::move(stream_config);
  info.node = *stream_config_node;

  // Expecting 1st node to be input node.
  EXPECT_EQ(NodeType::kInputStream, stream_config_node->type);

  // Using ML|DS stream in Monitor configuration for test here.
  const auto* next_node = camera::GetNextNodeInPipeline(info.stream_config.properties.stream_type(),
                                                        *stream_config_node);
  ASSERT_NE(nullptr, next_node);

  // Expecting 2nd node to be input node.
  EXPECT_EQ(NodeType::kGdc, next_node->type);

  next_node =
      camera::GetNextNodeInPipeline(info.stream_config.properties.stream_type(), *next_node);
  ASSERT_NE(nullptr, next_node);

  // Expecting 3rd node to be input node.
  EXPECT_EQ(NodeType::kOutputStream, next_node->type);
}

TEST_F(ControllerProtocolTest, TestFrameErrors) {
  auto stream_type = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();

  // Invoke OnFrameAvailable() for the ISP node. Buffer index = 1.
  frame_available_info_t frame_info = {
      .frame_status = FRAME_STATUS_ERROR_BUFFER_FULL,
      .buffer_id = 1,
      .metadata =
          {
              .timestamp = static_cast<uint64_t>(zx_clock_get_monotonic()),
              .image_format_index = 0,
              .input_buffer_index = 0,
          },
  };

  // Provide a frame to  the ISP node. In this configuration the
  // first 3 frames will be dropped (released back to the ISP).
  EXPECT_NO_FATAL_FAILURE(fr_head_node->OnReadyToProcess(&frame_info));
  RunLoopUntilIdle();
  // Ensure that the frame is not released.
  EXPECT_FALSE(fake_isp_.frame_released());

  frame_info.frame_status = FRAME_STATUS_OK;
  EXPECT_NO_FATAL_FAILURE(fr_head_node->OnReadyToProcess(&frame_info));
  RunLoopUntilIdle();
  // Ensure that the frame is released.
  EXPECT_TRUE(fake_isp_.frame_released());
}

TEST_F(ControllerProtocolTest, TestMultipleStartStreaming) {
  auto stream_type = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());

  // Set streaming on.
  auto initial_start_count = fake_isp_.start_stream_counter();
  output_node->client_stream()->Start();
  auto updated_start_count = fake_isp_.start_stream_counter();
  EXPECT_EQ(updated_start_count, initial_start_count + 1);
  EXPECT_NO_FATAL_FAILURE(output_node->client_stream()->Start());
  // Check if the ISP StartStream() was invoked twice.
  EXPECT_EQ(fake_isp_.start_stream_counter(), updated_start_count + 1);
}

TEST_F(ControllerProtocolTest, TestMultipleStopStreaming) {
  auto stream_type = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());

  // Set streaming off.
  auto initial_stop_count = fake_isp_.stop_stream_counter();
  output_node->client_stream()->Stop();
  auto updated_stop_count = fake_isp_.stop_stream_counter();
  EXPECT_EQ(updated_stop_count, initial_stop_count + 1);
  EXPECT_NO_FATAL_FAILURE(output_node->client_stream()->Stop());
  EXPECT_EQ(fake_isp_.stop_stream_counter(), updated_stop_count + 1);
}

TEST_F(ControllerProtocolTest, TestMonitorMultiStreamFRBadOrder) {
  auto stream_type1 = kStreamTypeDS | kStreamTypeML;
  auto stream_type2 = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream1;
  fuchsia::camera2::StreamPtr stream2;

  bool stream_alive = true;
  stream2.set_error_handler([&](zx_status_t /* status*/) { stream_alive = false; });

  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type1, stream1));
  EXPECT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type2, stream2));
  EXPECT_FALSE(stream_alive);
}

TEST_F(ControllerProtocolTest, TestMonitorMultiStreamFR) {
  fuchsia::camera2::StreamPtr stream1;
  fuchsia::camera2::StreamPtr stream2;

  auto stream_type1 = kStreamTypeDS | kStreamTypeML;
  auto stream_type2 = kStreamTypeFR | kStreamTypeML;

  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type2, stream2));
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type1, stream1));

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* fr_ml_output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());
  auto* gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(1).get());
  auto* ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

  // Validate input node.
  EXPECT_TRUE(HasAllStreams(fr_head_node->configured_streams(), {stream_type1, stream_type2}));
  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type1));
  EXPECT_TRUE(fr_head_node->is_stream_supported(stream_type2));

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

  auto output_formats = GetOutputFormats(stream1);
  EXPECT_EQ(output_formats.size(), 1u);

  output_formats = GetOutputFormats(stream2);
  EXPECT_EQ(output_formats.size(), 1u);
}

TEST_F(ControllerProtocolTest, TestInUseBufferCounts) {
  auto stream_type = kStreamTypeFR | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  bool stream_alive = true;
  stream.set_error_handler([&](zx_status_t /*status*/) { stream_alive = false; });

  bool frame_received = false;
  stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
    frame_received = true;
  };

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();

  // Start streaming.
  async::PostTask(dispatcher(), [&stream]() { stream->Start(); });
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

  for (uint32_t i = 0; i < kNumBuffers; i++) {
    frame_info.buffer_id = i;
    async::PostTask(dispatcher(),
                    [&fr_head_node, frame_info] { fr_head_node->OnReadyToProcess(&frame_info); });
    RunLoopUntilIdle();
  }

  while (!frame_received) {
    RunLoopUntilIdle();
  }

  EXPECT_TRUE(frame_received);
  EXPECT_EQ(fr_head_node->get_in_use_buffer_count(0), 0u);
  EXPECT_EQ(fr_head_node->get_in_use_buffer_count(1), 0u);
  EXPECT_EQ(fr_head_node->get_in_use_buffer_count(2), 1u);
  EXPECT_EQ(fr_head_node->get_in_use_buffer_count(3), 0u);

  async::PostTask(dispatcher(), [&stream]() { stream->ReleaseFrame(2); });
  RunLoopUntilIdle();

  EXPECT_EQ(fr_head_node->get_in_use_buffer_count(2), 0u);

  stream->Stop();
}

TEST_F(ControllerProtocolTest, TestOutputNode) {
  auto stream_type = kStreamTypeDS | kStreamTypeML;
  auto* stream_config_node = GetStreamConfigNode(SherlockConfigs::MONITORING, stream_type);
  ASSERT_NE(nullptr, stream_config_node);
  StreamCreationData info;
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.properties.set_stream_type(stream_type);
  info.stream_config = std::move(stream_config);
  info.node = *stream_config_node;

  ControllerMemoryAllocator allocator(std::move(sysmem_allocator2_));

  // Testing successful creation of |OutputNode|.
  auto input_result = GetInputNode(allocator, &info);
  auto output_result =
      OutputNode::CreateOutputNode(dispatcher(), &info, input_result.value().get(), info.node);
  EXPECT_TRUE(output_result.is_ok());
  ASSERT_NE(nullptr, output_result.value());
  EXPECT_NE(nullptr, output_result.value()->client_stream());
  EXPECT_EQ(NodeType::kOutputStream, output_result.value()->type());

  // Passing invalid arguments.
  output_result =
      OutputNode::CreateOutputNode(nullptr, &info, input_result.value().get(), info.node);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());

  output_result =
      OutputNode::CreateOutputNode(dispatcher(), nullptr, input_result.value().get(), info.node);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());

  output_result = OutputNode::CreateOutputNode(dispatcher(), &info, nullptr, info.node);
  EXPECT_EQ(ZX_ERR_INVALID_ARGS, output_result.error());
}

TEST_F(ControllerProtocolTest, TestGdcNode) {
  auto* stream_config_node =
      GetStreamConfigNode(SherlockConfigs::MONITORING, kStreamTypeDS | kStreamTypeML);
  ASSERT_NE(nullptr, stream_config_node);
  StreamCreationData info;
  fuchsia::camera2::hal::StreamConfig stream_config;
  stream_config.properties.set_stream_type(kStreamTypeDS | kStreamTypeML);
  info.stream_config = std::move(stream_config);
  info.node = *stream_config_node;
  ControllerMemoryAllocator allocator(std::move(sysmem_allocator2_));

  auto input_result = GetInputNode(allocator, &info);
  // Testing successful creation of |GdcNode|.
  auto* next_node_internal = GetNextNodeInPipeline(kStreamTypeDS | kStreamTypeML, info.node);
  ASSERT_NE(nullptr, next_node_internal);
  auto gdc_result = GdcNode::CreateGdcNode(allocator, dispatcher(), fake_ddk::kFakeParent, gdc_,
                                           &info, input_result.value().get(), *next_node_internal);
  EXPECT_TRUE(gdc_result.is_ok());
  ASSERT_NE(nullptr, gdc_result.value());
  EXPECT_EQ(NodeType::kGdc, gdc_result.value()->type());
}

TEST_F(ControllerProtocolTest, TestReleaseAfterStopStreaming) {
  auto stream_type = kStreamTypeDS | kStreamTypeML;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream]() { stream->Start(); });
  RunLoopUntilIdle();

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(0).get());
  auto* ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

  EXPECT_FALSE(fake_isp_.frame_released());

  EXPECT_TRUE(fr_head_node->enabled());
  EXPECT_TRUE(gdc_node->enabled());
  EXPECT_TRUE(ds_ml_output_node->enabled());

  // Stop streaming.
  async::PostTask(dispatcher(), [&stream]() { stream->Stop(); });
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
  EXPECT_NO_FATAL_FAILURE(fr_head_node->OnReadyToProcess(&frame_info));
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_isp_.frame_released());

  // Making a frame available to GDC node.
  // Expecting the frame to be released since node is disabled.
  EXPECT_NO_FATAL_FAILURE(gdc_node->OnFrameAvailable(&frame_info));
  RunLoopUntilIdle();
  EXPECT_TRUE(fake_gdc_.frame_released());
}

TEST_F(ControllerProtocolTest, TestEnabledDisableStreaming) {
  fuchsia::camera2::StreamPtr stream_ds;
  fuchsia::camera2::StreamPtr stream_fr;

  auto stream_type_ds = kStreamTypeDS | kStreamTypeML;
  auto stream_type_fr = kStreamTypeFR | kStreamTypeML;

  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_fr, stream_fr));
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_ds, stream_ds));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream_fr]() { stream_fr->Start(); });
  async::PostTask(dispatcher(), [&stream_ds]() { stream_ds->Start(); });
  RunLoopUntilIdle();

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* fr_ml_output_node = static_cast<OutputNode*>(fr_head_node->child_nodes().at(0).get());
  auto* gdc_node = static_cast<GdcNode*>(fr_head_node->child_nodes().at(1).get());
  auto* ds_ml_output_node = static_cast<OutputNode*>(gdc_node->child_nodes().at(0).get());

  EXPECT_TRUE(fr_head_node->enabled());
  EXPECT_TRUE(fr_ml_output_node->enabled());
  EXPECT_TRUE(gdc_node->enabled());
  EXPECT_TRUE(ds_ml_output_node->enabled());

  async::PostTask(dispatcher(), [this]() { pipeline_manager_->StopStreaming(); });
  RunLoopUntilIdle();

  EXPECT_FALSE(fr_head_node->enabled());
  EXPECT_FALSE(fr_ml_output_node->enabled());
  EXPECT_FALSE(gdc_node->enabled());
  EXPECT_FALSE(ds_ml_output_node->enabled());

  async::PostTask(dispatcher(), [this]() { pipeline_manager_->StartStreaming(); });
  RunLoopUntilIdle();

  EXPECT_TRUE(fr_head_node->enabled());
  EXPECT_TRUE(fr_ml_output_node->enabled());
  EXPECT_TRUE(gdc_node->enabled());
  EXPECT_TRUE(ds_ml_output_node->enabled());
}

TEST_F(ControllerProtocolTest, TestMultipleFrameRates) {
  auto fr_stream_type = kStreamTypeFR | kStreamTypeML;
  auto ds_stream_type = kStreamTypeMonitoring;
  fuchsia::camera2::StreamPtr fr_stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, fr_stream_type, fr_stream));

  fuchsia::camera2::StreamPtr ds_stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, ds_stream_type, ds_stream));

  bool fr_stream_alive = true;
  fr_stream.set_error_handler([&](zx_status_t /*status*/) { fr_stream_alive = false; });

  bool fr_frame_received = false;
  uint32_t fr_frame_index = 0;
  fr_stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
    fr_frame_received = true;
    fr_frame_index = info.buffer_id;
  };

  bool ds_stream_alive = true;
  ds_stream.set_error_handler([&](zx_status_t /*status*/) { ds_stream_alive = false; });

  bool ds_frame_received = false;
  uint32_t ds_frame_index = 0;
  uint32_t ds_frame_count = 0;
  ds_stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
    ds_frame_received = true;
    ds_frame_index = info.buffer_id;
    ds_frame_count++;
  };

  auto* fr_head_node = pipeline_manager_->full_resolution_stream();
  auto* ds_head_node = pipeline_manager_->downscaled_resolution_stream();

  // Start streaming.
  async::PostTask(dispatcher(), [&fr_stream]() { fr_stream->Start(); });
  async::PostTask(dispatcher(), [&ds_stream]() { ds_stream->Start(); });
  RunLoopUntilIdle();

  // Invoke OnFrameAvailable() for the ISP node. Buffer index = 1.
  frame_available_info_t frame_info = {
      .frame_status = FRAME_STATUS_OK,
      .buffer_id = 0,
      .metadata =
          {
              .timestamp = static_cast<uint64_t>(zx_clock_get_monotonic()),
              .image_format_index = 0,
              .input_buffer_index = 0,
          },
  };

  for (uint32_t i = 0; i < kNumBuffers; i++) {
    frame_info.buffer_id = i;
    async::PostTask(dispatcher(), [&frame_info, &fr_head_node]() {
      fr_head_node->OnReadyToProcess(&frame_info);
    });
    RunLoopUntilIdle();
    async::PostTask(dispatcher(), [&frame_info, &ds_head_node]() {
      ds_head_node->OnReadyToProcess(&frame_info);
    });
    RunLoopUntilIdle();
  }

  EXPECT_EQ(fr_frame_index, 2u);
  EXPECT_EQ(ds_frame_index, 4u);
  EXPECT_EQ(ds_frame_count, 5u);
}

TEST_F(ControllerProtocolTest, TestFindGraphHead) {
  auto fr_stream_type = kStreamTypeFR | kStreamTypeML;
  auto ds_stream_type = kStreamTypeMonitoring;
  fuchsia::camera2::StreamPtr fr_stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, fr_stream_type, fr_stream));

  fuchsia::camera2::StreamPtr ds_stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, ds_stream_type, ds_stream));

  auto result = pipeline_manager_->FindGraphHead(fr_stream_type);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION, result.value().second);

  result = pipeline_manager_->FindGraphHead(ds_stream_type);
  EXPECT_FALSE(result.is_error());
  EXPECT_EQ(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION, result.value().second);

  result = pipeline_manager_->FindGraphHead(kStreamTypeVideo);
  EXPECT_TRUE(result.is_error());
  EXPECT_EQ(result.error(), ZX_ERR_BAD_STATE);
}

TEST_F(ControllerProtocolTest, TestResolutionChange) {
  auto ds_stream_type = kStreamTypeMonitoring;
  fuchsia::camera2::StreamPtr ds_stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, ds_stream_type, ds_stream));

  auto* ds_head_node = pipeline_manager_->downscaled_resolution_stream();
  auto* gdc_node = static_cast<GdcNode*>(ds_head_node->child_nodes().at(0).get());
  auto* ge2d_node = static_cast<GdcNode*>(gdc_node->child_nodes().at(0).get());
  auto* output_node = static_cast<GdcNode*>(ge2d_node->child_nodes().at(0).get());

  bool ds_stream_alive = true;
  ds_stream.set_error_handler([&](zx_status_t /*status*/) { ds_stream_alive = false; });

  uint32_t old_resolution = 0;
  uint32_t new_resolution = 1;
  uint32_t ds_frame_count = 0;
  ds_stream.events().OnFrameAvailable = [&](fuchsia::camera2::FrameAvailableInfo info) {
    ds_frame_count++;
    if (ds_frame_count > 1) {
      EXPECT_EQ(gdc_node->current_image_format_index(), new_resolution);
      EXPECT_EQ(ge2d_node->current_image_format_index(), new_resolution);
      EXPECT_EQ(output_node->current_image_format_index(), new_resolution);
      EXPECT_EQ(new_resolution, info.metadata.image_format_index());
    } else {
      EXPECT_EQ(gdc_node->current_image_format_index(), old_resolution);
      EXPECT_EQ(ge2d_node->current_image_format_index(), old_resolution);
      EXPECT_EQ(output_node->current_image_format_index(), old_resolution);
      EXPECT_EQ(old_resolution, info.metadata.image_format_index());
    }
  };

  EXPECT_EQ(gdc_node->type(), NodeType::kGdc);

  // Start streaming.
  async::PostTask(dispatcher(), [&ds_stream]() { ds_stream->Start(); });
  RunLoopUntilIdle();

  // Invoke OnFrameAvailable() for the ISP node.
  frame_available_info_t frame_info = {
      .frame_status = FRAME_STATUS_OK,
      .buffer_id = 0,
      .metadata =
          {
              .timestamp = static_cast<uint64_t>(zx_clock_get_monotonic()),
              .image_format_index = old_resolution,
              .input_buffer_index = 0,
          },
  };
  // Post 1 frame with old resolution.
  async::PostTask(dispatcher(),
                  [&frame_info, &ds_head_node]() { ds_head_node->OnReadyToProcess(&frame_info); });
  RunLoopUntilIdle();

  auto callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    ds_stream->SetImageFormat(10u, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    });
  });
  RunLoopUntilIdle();
  ASSERT_EQ(callback_called, true);

  callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    ds_stream->SetImageFormat(new_resolution, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_OK);
    });
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  // Post other frames.
  for (uint32_t i = 1; i < kNumBuffers; i++) {
    async::PostTask(dispatcher(), [&frame_info, &ds_head_node, i]() {
      frame_info.buffer_id = i;
      ds_head_node->OnReadyToProcess(&frame_info);
    });
    RunLoopUntilIdle();
  }
  EXPECT_EQ(ds_frame_count, static_cast<uint32_t>(kNumBuffers));
}

TEST_F(ControllerProtocolTest, TestPipelineManagerShutdown) {
  fuchsia::camera2::StreamPtr stream_ds;
  fuchsia::camera2::StreamPtr stream_fr;

  auto stream_type_ds = kStreamTypeDS | kStreamTypeML;
  auto stream_type_fr = kStreamTypeFR | kStreamTypeML;

  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_fr, stream_fr));
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_ds, stream_ds));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream_fr]() { stream_fr->Start(); });
  async::PostTask(dispatcher(), [&stream_ds]() { stream_ds->Start(); });
  RunLoopUntilIdle();

  async::PostTask(dispatcher(), [this]() { pipeline_manager_->Shutdown(); });
  RunLoopUntilIdle();

  zx_signals_t pending;
  event_.wait_one(kPipelineManagerSignalExitDone, zx::time::infinite(), &pending);

  EXPECT_EQ(nullptr, pipeline_manager_->full_resolution_stream());
  EXPECT_EQ(nullptr, pipeline_manager_->downscaled_resolution_stream());
}

TEST_F(ControllerProtocolTest, TestStreamShutdownAfterPipelineShutdown) {
  fuchsia::camera2::StreamPtr stream_ds;
  fuchsia::camera2::StreamPtr stream_fr;

  auto stream_type_ds = kStreamTypeDS | kStreamTypeML;
  auto stream_type_fr = kStreamTypeFR | kStreamTypeML;

  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_fr, stream_fr));
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type_ds, stream_ds));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream_fr]() { stream_fr->Start(); });
  async::PostTask(dispatcher(), [&stream_ds]() { stream_ds->Start(); });
  RunLoopUntilIdle();

  async::PostTask(dispatcher(), [this]() { pipeline_manager_->Shutdown(); });
  async::PostTask(dispatcher(), [&stream_fr]() { stream_fr.Unbind(); });
  RunLoopUntilIdle();

  zx_signals_t pending;
  event_.wait_one(kPipelineManagerSignalExitDone, zx::time::infinite(), &pending);

  EXPECT_EQ(nullptr, pipeline_manager_->full_resolution_stream());
  EXPECT_EQ(nullptr, pipeline_manager_->downscaled_resolution_stream());
}

TEST_F(ControllerProtocolTest, TestCropRectChange) {
  auto stream_type = kStreamTypeVideo;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::VIDEO, stream_type, stream));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream]() { stream->Start(); });
  RunLoopUntilIdle();

  auto callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    stream->SetRegionOfInterest(0.0, 0.0, 0.0, 0.0, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_OK);
    });
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  // x_min > x_max
  callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    stream->SetRegionOfInterest(0.6, 0.0, 0.5, 0.0, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    });
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);

  // y_min > y_max
  callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    stream->SetRegionOfInterest(0.6, 0.0, 0.5, 0.0, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_ERR_INVALID_ARGS);
    });
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(ControllerProtocolTest, TestCropRectChangeInvalidStream) {
  auto stream_type = kStreamTypeMonitoring;
  fuchsia::camera2::StreamPtr stream;
  ASSERT_EQ(ZX_OK, SetupStream(SherlockConfigs::MONITORING, stream_type, stream));

  // Start streaming.
  async::PostTask(dispatcher(), [&stream]() { stream->Start(); });
  RunLoopUntilIdle();

  auto callback_called = false;
  async::PostTask(dispatcher(), [&]() {
    stream->SetRegionOfInterest(0.0, 0.0, 0.0, 0.0, [&](zx_status_t status) {
      callback_called = true;
      EXPECT_EQ(status, ZX_ERR_NOT_SUPPORTED);
    });
  });
  RunLoopUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(ControllerProtocolTest, LoadGdcConfig) {
#ifdef INTERNAL_ACCESS
  auto result = camera::LoadGdcConfiguration(fake_ddk::kFakeParent, GdcConfig::INVALID);
  EXPECT_TRUE(result.is_error());

  result = camera::LoadGdcConfiguration(fake_ddk::kFakeParent, GdcConfig::MONITORING_360p);
  EXPECT_FALSE(result.is_error());
#else
  GTEST_SKIP();
#endif
}
}  // namespace

}  // namespace camera
