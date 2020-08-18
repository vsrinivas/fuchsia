// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/output_node.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/drivers/controller/stream_protocol.h"

namespace camera {

constexpr auto kTag = "camera_controller_output_node";

fit::result<OutputNode*, zx_status_t> OutputNode::CreateOutputNode(
    async_dispatcher_t* dispatcher, StreamCreationData* info, ProcessNode* parent_node,
    const InternalConfigNode& internal_output_node) {
  if (dispatcher == nullptr || info == nullptr || parent_node == nullptr) {
    FX_LOGST(DEBUG, kTag) << "Invalid input parameters";
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  fuchsia::sysmem::BufferCollectionInfo_2 unused_buffer_collection;

  auto output_node = std::make_unique<camera::OutputNode>(
      dispatcher, parent_node, internal_output_node, std::move(unused_buffer_collection),
      info->stream_type(), info->image_format_index);
  if (!output_node) {
    FX_LOGST(ERROR, kTag) << "Failed to create output ProcessNode";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  auto client_stream = std::make_unique<camera::StreamImpl>(output_node.get());
  if (!client_stream) {
    FX_LOGST(ERROR, kTag) << "Failed to create StreamImpl";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // Set the client stream.
  output_node->set_client_stream(std::move(client_stream));
  auto result = fit::ok(output_node.get());

  // Add child node.
  parent_node->AddChildNodeInfo(std::move(output_node));
  return result;
}

void OutputNode::OnReadyToProcess(const frame_available_info_t* info) {
  TRACE_DURATION("camera", "OutputNode::OnReadyToProcess", "info->buffer_id", info->buffer_id);
  if (enabled_) {
    ZX_ASSERT(client_stream_ != nullptr);
    client_stream_->FrameReady(info);
    return;
  }
  // Since streaming is disabled the incoming frame is released
  // so it gets added back to the pool.
  OnReleaseFrame(info->buffer_id);
}

void OutputNode::OnReleaseFrame(uint32_t buffer_index) {
  TRACE_DURATION("camera", "OutputNode::OnReleaseFrame", "buffer_index", buffer_index);
  parent_node_->OnReleaseFrame(buffer_index);
}

zx_status_t OutputNode::Attach(zx::channel channel, fit::function<void(void)> disconnect_handler) {
  return client_stream_->Attach(std::move(channel), std::move(disconnect_handler));
}

}  // namespace camera
