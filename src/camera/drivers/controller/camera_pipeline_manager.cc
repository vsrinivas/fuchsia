// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "camera_pipeline_manager.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

#include "src/lib/syslog/cpp/logger.h"
#include "stream_protocol.h"

namespace camera {

const InternalConfigNode* CameraPipelineManager::GetNextNodeInPipeline(
    CameraPipelineInfo* info, const InternalConfigNode& node) {
  for (const auto& child_node : node.child_nodes) {
    if (child_node.output_stream_type == info->stream_config->properties.stream_type()) {
      return &child_node;
    }
  }
  return nullptr;
}

// Temporary code to convert into buffercollectioninfo
static void ConvertToBufferCollectionInfo(
    fuchsia::sysmem::BufferCollectionInfo_2* buffer_collection,
    fuchsia_sysmem_BufferCollectionInfo* old_buffer_collection) {
  old_buffer_collection->buffer_count = buffer_collection->buffer_count;
  old_buffer_collection->format.image.width =
      buffer_collection->settings.image_format_constraints.max_coded_width;
  old_buffer_collection->format.image.height =
      buffer_collection->settings.image_format_constraints.max_coded_height;
  old_buffer_collection->format.image.layers =
      buffer_collection->settings.image_format_constraints.layers;
  old_buffer_collection->format.image.pixel_format =
      *reinterpret_cast<const fuchsia_sysmem_PixelFormat*>(
          &buffer_collection->settings.image_format_constraints.pixel_format);
  old_buffer_collection->format.image.color_space =
      *reinterpret_cast<const fuchsia_sysmem_ColorSpace*>(
          &buffer_collection->settings.image_format_constraints.color_space);
  old_buffer_collection->format.image.planes[0].bytes_per_row =
      buffer_collection->settings.image_format_constraints.max_bytes_per_row;
  for (uint32_t i = 0; i < buffer_collection->buffer_count; ++i) {
    old_buffer_collection->vmos[i] = buffer_collection->buffers[i].vmo.release();
  }
  old_buffer_collection->vmo_size = buffer_collection->settings.buffer_settings.size_bytes;

  // End of temporary code
}

// NOTE: This API currently supports only debug config
// At a later point it will also need to take care of scenarios where same source stream
// provides multiple output streams.
zx_status_t CameraPipelineManager::GetBuffers(
    const InternalConfigNode& producer, CameraPipelineInfo* info,
    fuchsia::sysmem::BufferCollectionInfo_2* out_buffers) {
  auto consumer = GetNextNodeInPipeline(info, producer);
  if (!consumer) {
    FX_LOGS(ERROR) << "Failed to get next node";
    return ZX_ERR_INTERNAL;
  }
  // If the consumer is the client, we use the client buffers
  if (consumer->type == kOutputStream) {
    *out_buffers = std::move(info->output_buffers);
    return ZX_OK;
  }
  // Otherwise we allocate the buffers.
  // TODO(braval): Not needed at the moment so returning ZX_ERR_NOT_SUPPORTED;
  return ZX_ERR_NOT_SUPPORTED;
}
zx_status_t CameraPipelineManager::ConfigureInputNode(
    CameraPipelineInfo* info, std::shared_ptr<CameraProcessNode>* out_processing_node) {
  uint8_t isp_stream_type;
  if (info->node.input_stream_type == fuchsia::camera2::CameraStreamType::FULL_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_FULL_RESOLUTION;
  } else {
    isp_stream_type = STREAM_TYPE_DOWNSCALED;
  }

  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  auto status = GetBuffers(info->node, info, &buffers);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to get buffers";
    return status;
  }

  // Temporary conversion since ISP protocol
  // accepts only old bufferCollectionInfo
  fuchsia_sysmem_BufferCollectionInfo old_buffer_collection;
  ConvertToBufferCollectionInfo(&buffers, &old_buffer_collection);

  // Create Input Node
  auto processing_node =
      std::make_shared<camera::CameraProcessNode>(info->node.type, old_buffer_collection);
  if (!processing_node) {
    FX_LOGS(ERROR) << "Failed to create ISP stream protocol";
    return ZX_ERR_NO_MEMORY;
  }

  // Create stream with ISP
  auto isp_stream_protocol = std::make_unique<camera::IspStreamProtocol>();
  if (!isp_stream_protocol) {
    FX_LOGS(ERROR) << "Failed to create ISP stream protocol";
    return ZX_ERR_INTERNAL;
  }

  status = isp_.CreateOutputStream(
      &old_buffer_collection, reinterpret_cast<const frame_rate_t*>(&info->node.output_frame_rate),
      STREAM_TYPE_FULL_RESOLUTION, processing_node->callback(), isp_stream_protocol->protocol());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create output stream on ISP";
    return ZX_ERR_INTERNAL;
  }

  // Update the input node with the ISP stream protocol
  processing_node->set_isp_stream_protocol(std::move(isp_stream_protocol));

  *out_processing_node = std::move(processing_node);
  return ZX_OK;
}

zx_status_t CameraPipelineManager::ConfigureOutputNode(
    const std::shared_ptr<CameraProcessNode>& parent_node, const InternalConfigNode* node,
    std::shared_ptr<CameraProcessNode>* output_processing_node) {
  // Create Output Node
  auto output_node = std::make_shared<camera::CameraProcessNode>(node->type);
  if (!output_node) {
    FX_LOGS(ERROR) << "Failed to create output CameraProcessNode";
    return ZX_ERR_NO_MEMORY;
  }

  auto client_stream = std::make_unique<camera::StreamImpl>(dispatcher_, output_node);
  if (!client_stream) {
    FX_LOGS(ERROR) << "Failed to create StreamImpl";
    return ZX_ERR_INTERNAL;
  }

  // Set the parent node
  output_node->set_parent_node(parent_node);

  // Set the client stream
  output_node->set_client_stream(std::move(client_stream));

  *output_processing_node = output_node;

  // Add child node info.
  ChildNodeInfo child_info;
  child_info.child_node = std::move(output_node);
  child_info.stream_type = node->output_stream_type;
  child_info.output_frame_rate = node->output_frame_rate;
  parent_node->AddChildNodeInfo(std::move(child_info));
  return ZX_OK;
}

zx_status_t CameraPipelineManager::CreateGraph(
    CameraPipelineInfo* info, const std::shared_ptr<CameraProcessNode>& parent_node,
    std::shared_ptr<CameraProcessNode>* output_processing_node) {
  auto next_node = GetNextNodeInPipeline(info, info->node);
  if (!next_node) {
    FX_LOGS(ERROR) << "Failed to get next node";
    return ZX_ERR_INTERNAL;
  }

  switch (next_node->type) {
    // Input Node
    case NodeType::kInputStream: {
      FX_LOGS(ERROR) << "Child node cannot be input node";
      return ZX_ERR_INVALID_ARGS;
    }
    // GDC
    case NodeType::kGdc: {
      return ZX_ERR_NOT_SUPPORTED;
    }
    // GE2D
    case NodeType::kGe2d: {
      return ZX_ERR_NOT_SUPPORTED;
    }
    // Output Node
    case NodeType::kOutputStream: {
      auto status = ConfigureOutputNode(parent_node, next_node, output_processing_node);
      if (status != ZX_OK) {
        FX_LOGS(ERROR) << "Failed to configure Output Node";
        // TODO(braval): Handle already configured nodes
        return status;
      }
      break;
    }
    default: {
      return ZX_ERR_NOT_SUPPORTED;
    }
  }
  return ZX_OK;
}

zx_status_t CameraPipelineManager::ConfigureStreamPipeline(
    CameraPipelineInfo* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream) {
  // Input Validations
  if (info == nullptr || info->stream_config == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Here at top level we check what type of input stream do we have to deal with
  if (info->node.input_stream_type == fuchsia::camera2::CameraStreamType::FULL_RESOLUTION) {
    if (full_resolution_stream_) {
      // TODO(braval): If valid it means we need to modify existing graph
      // TODO(braval): Check if same stream is requested, if so do not allow
      // Currently we will only be not allowing since we only support ISP debug config.
      FX_PLOGS(ERROR, ZX_ERR_ALREADY_BOUND) << "Stream already bound";
      return ZX_ERR_ALREADY_BOUND;
    }

    // Configure Input node
    auto status = ConfigureInputNode(info, &full_resolution_stream_);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to ConfigureInputNode";
      return status;
    }

    std::shared_ptr<CameraProcessNode> output_processing_node;
    status = CreateGraph(info, full_resolution_stream_, &output_processing_node);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to CreateGraph";
      return status;
    }

    status = output_processing_node->client_stream()->Attach(stream.TakeChannel(), []() {
      FX_LOGS(INFO) << "Stream client disconnected";
      // stream_ = nullptr;
    });
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to bind output stream";
      return status;
    }

  } else {
    // Currently only supporting ISP debug config which has only FR stream
    FX_LOGS(ERROR) << "Invalid input stream type";
    return ZX_ERR_INVALID_ARGS;
  }
  return ZX_OK;
}

}  // namespace camera
