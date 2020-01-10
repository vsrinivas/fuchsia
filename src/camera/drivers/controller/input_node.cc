// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/input_node.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/drivers/controller/graph_utils.h"
#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto kTag = "camera_controller_input_node";

fit::result<std::unique_ptr<InputNode>, zx_status_t> InputNode::CreateInputNode(
    StreamCreationData* info, const ControllerMemoryAllocator& memory_allocator,
    async_dispatcher_t* dispatcher, const ddk::IspProtocolClient& isp) {
  uint8_t isp_stream_type;
  if (info->node.input_stream_type == fuchsia::camera2::CameraStreamType::FULL_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_FULL_RESOLUTION;
  } else if (info->node.input_stream_type ==
             fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_DOWNSCALED;
  } else {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  auto result = camera::GetBuffers(memory_allocator, info->node, info, nullptr);
  if (result.is_error()) {
    FX_PLOGST(ERROR, kTag, result.error()) << "Failed to get buffers";
    return fit::error(result.error());
  }
  auto buffers = std::move(result.value());

  // Use a BufferCollectionHelper to manage the conversion
  // between buffer collection representations.
  BufferCollectionHelper buffer_collection_helper(buffers);

  auto image_format = ConvertHlcppImageFormat2toCType(&info->node.image_formats[0]);

  // Create Input Node
  auto processing_node = std::make_unique<camera::InputNode>(
      info->node.image_formats, std::move(buffers), info->stream_config->properties.stream_type(),
      info->node.supported_streams, dispatcher, isp);
  if (!processing_node) {
    FX_LOGST(ERROR, kTag) << "Failed to create Input node";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Create stream with ISP
  auto isp_stream_protocol = std::make_unique<camera::IspStreamProtocol>();
  if (!isp_stream_protocol) {
    FX_LOGST(ERROR, kTag) << "Failed to create ISP stream protocol";
    return fit::error(ZX_ERR_INTERNAL);
  }

  auto status = isp.CreateOutputStream(
      buffer_collection_helper.GetC(), &image_format,
      reinterpret_cast<const frame_rate_t*>(&info->node.output_frame_rate), isp_stream_type,
      processing_node->isp_frame_callback(), isp_stream_protocol->protocol());
  if (status != ZX_OK) {
    FX_PLOGST(ERROR, kTag, status) << "Failed to create output stream on ISP";
    return fit::error(status);
  }

  // Update the input node with the ISP stream protocol
  processing_node->set_isp_stream_protocol(std::move(isp_stream_protocol));
  return fit::ok(std::move(processing_node));
}

void InputNode::OnReadyToProcess(uint32_t buffer_index) {
  ZX_ASSERT_MSG(false, "Invalid for InputNode");
}

void InputNode::OnFrameAvailable(const frame_available_info_t* info) {
  if (enabled_) {
    ProcessNode::OnFrameAvailable(info);
  } else {
    isp_stream_protocol_->ReleaseFrame(info->buffer_id);
  }
}

void InputNode::OnReleaseFrame(uint32_t buffer_index) {
  fbl::AutoLock al(&in_use_buffer_lock_);
  ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
  in_use_buffer_count_[buffer_index]--;
  if (in_use_buffer_count_[buffer_index] != 0) {
    return;
  }
  isp_stream_protocol_->ReleaseFrame(buffer_index);
}

void InputNode::OnStartStreaming() {
  if (!enabled_) {
    enabled_ = true;
    isp_stream_protocol_->Start();
  }
}

void InputNode::OnStopStreaming() {
  if (enabled_) {
    if (AllChildNodesDisabled()) {
      enabled_ = false;
      isp_stream_protocol_->Stop();
    }
  }
}

}  // namespace camera
