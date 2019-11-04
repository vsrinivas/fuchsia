// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pipeline_manager.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include <src/lib/fxl/logging.h>

#include "src/lib/syslog/cpp/logger.h"
#include "stream_protocol.h"

namespace camera {

const InternalConfigNode* PipelineManager::GetNextNodeInPipeline(PipelineInfo* info,
                                                                 const InternalConfigNode& node) {
  for (const auto& child_node : node.child_nodes) {
    if (child_node.output_stream_type == info->stream_config->properties.stream_type()) {
      return &child_node;
    }
  }
  return nullptr;
}

// Temporary code to convert into buffercollectioninfo
static zx_status_t ConvertToBufferCollectionInfo(
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
    // We duplicate the handles since we need to new version
    // as well to send it to GDC
    zx::vmo vmo;
    auto status = buffer_collection->buffers[i].vmo.duplicate(ZX_RIGHT_SAME_RIGHTS, &vmo);
    if (status != ZX_OK) {
      FX_PLOGS(ERROR, status) << "Failed to dup VMO";
      return status;
    }
    old_buffer_collection->vmos[i] = vmo.release();
  }
  old_buffer_collection->vmo_size = buffer_collection->settings.buffer_settings.size_bytes;
  // End of temporary code
  return ZX_OK;
}

// NOTE: This API currently supports only debug config
// At a later point it will also need to take care of scenarios where same source stream
// provides multiple output streams.
zx_status_t PipelineManager::GetBuffers(const InternalConfigNode& producer, PipelineInfo* info,
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

fit::result<std::unique_ptr<ProcessNode>, zx_status_t> PipelineManager::CreateInputNode(
    PipelineInfo* info) {
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
    return fit::error(status);
  }

  // Temporary conversion since ISP protocol
  // accepts only old bufferCollectionInfo
  fuchsia_sysmem_BufferCollectionInfo old_buffer_collection;
  ConvertToBufferCollectionInfo(&buffers, &old_buffer_collection);

  // Create Input Node
  auto processing_node = std::make_unique<camera::ProcessNode>(info->node.type, std::move(buffers),
                                                               old_buffer_collection);
  if (!processing_node) {
    FX_LOGS(ERROR) << "Failed to create ISP stream protocol";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  // Create stream with ISP
  auto isp_stream_protocol = std::make_unique<camera::IspStreamProtocol>();
  if (!isp_stream_protocol) {
    FX_LOGS(ERROR) << "Failed to create ISP stream protocol";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // TODO(braval): create FR or DS depending on what stream is requested
  status = isp_.CreateOutputStream(
      &old_buffer_collection, reinterpret_cast<const frame_rate_t*>(&info->node.output_frame_rate),
      STREAM_TYPE_FULL_RESOLUTION, processing_node->callback(), isp_stream_protocol->protocol());
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to create output stream on ISP";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // Update the input node with the ISP stream protocol
  processing_node->set_isp_stream_protocol(std::move(isp_stream_protocol));

  return fit::ok(std::move(processing_node));
}

fit::result<ProcessNode*, zx_status_t> PipelineManager::CreateOutputNode(
    ProcessNode* parent_node, const InternalConfigNode& internal_output_node) {
  // Create Output Node
  auto output_node = std::make_unique<camera::ProcessNode>(internal_output_node.type, parent_node);
  if (!output_node) {
    FX_LOGS(ERROR) << "Failed to create output ProcessNode";
    return fit::error(ZX_ERR_NO_MEMORY);
  }

  auto client_stream = std::make_unique<camera::StreamImpl>(dispatcher_, output_node.get());
  if (!client_stream) {
    FX_LOGS(ERROR) << "Failed to create StreamImpl";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // Set the client stream
  output_node->set_client_stream(std::move(client_stream));
  auto result = fit::ok(output_node.get());

  // Add child node info.
  ChildNodeInfo child_info;
  child_info.child_node = std::move(output_node);
  child_info.stream_type = internal_output_node.output_stream_type;
  child_info.output_frame_rate = internal_output_node.output_frame_rate;
  parent_node->AddChildNodeInfo(child_info);
  return result;
}

const char* ToString(const camera::GdcConfig& config_type) {
  switch (config_type) {
    case GdcConfig::MONITORING_360p:
      return "config_1152x1440_to_512x384_Crop_Rotate.bin";
    case GdcConfig::MONITORING_480p:
      return "config_1152x1440_to_720x540_Crop_Rotate.bin";
    case GdcConfig::MONITORING_720p:
      return "config_1152x1440_to_1152x864_Crop_Rotate.bin";
    case GdcConfig::MONITORING_ML:
      return "config_001_2176x2720-to-640x512-RS-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE:
      return "config_002_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_EXTENDED_FOV:
      return "config_003_2176x2720-to-2240x1792-DKCR-YUV420SemiPlanar.bin";
    case GdcConfig::VIDEO_CONFERENCE_ML:
      return "config_001_2240x1792-to-640x512-S-YUV420SemiPlanar.bin";
    case GdcConfig::INVALID:
    default:
      return nullptr;
  }
}

zx_status_t PipelineManager::LoadGdcConfiguration(const camera::GdcConfig& config_type,
                                                  zx_handle_t* vmo) {
  if (config_type == GdcConfig::INVALID) {
    FX_LOGS(ERROR) << "Invalid GDC configuration type";
    return ZX_ERR_INVALID_ARGS;
  }

  if (vmo == nullptr) {
    FX_LOGS(ERROR) << "Invalid VMO pointer";
    return ZX_ERR_INVALID_ARGS;
  }

  size_t size;
  auto status = load_firmware(device_, ToString(config_type), vmo, &size);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to load the GDC firmware";
    return status;
  }
  return ZX_OK;
}

fit::result<ProcessNode*, zx_status_t> PipelineManager::CreateGraph(PipelineInfo* info,
                                                                    ProcessNode* parent_node) {
  fit::result<ProcessNode*, zx_status_t> result;
  auto next_node_internal = GetNextNodeInPipeline(info, info->node);
  if (!next_node_internal) {
    FX_LOGS(ERROR) << "Failed to get next node";
    return fit::error(ZX_ERR_INTERNAL);
  }

  switch (next_node_internal->type) {
    // Input Node
    case NodeType::kInputStream: {
      FX_LOGS(ERROR) << "Child node cannot be input node";
      return fit::error(ZX_ERR_INVALID_ARGS);
    }
    // GDC
    case NodeType::kGdc: {
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // GE2D
    case NodeType::kGe2d: {
      return fit::error(ZX_ERR_NOT_SUPPORTED);
    }
    // Output Node
    case NodeType::kOutputStream: {
      result = CreateOutputNode(parent_node, *next_node_internal);
      if (result.is_error()) {
        FX_LOGS(ERROR) << "Failed to configure Output Node";
        // TODO(braval): Handle already configured nodes
        return result;
      }
      break;
    }
    default: { return fit::error(ZX_ERR_NOT_SUPPORTED); }
  }
  return result;
}

zx_status_t PipelineManager::ConfigureStreamPipeline(
    PipelineInfo* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream) {
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
    auto input_result = CreateInputNode(info);
    if (input_result.is_error()) {
      FX_PLOGS(ERROR, input_result.error()) << "Failed to ConfigureInputNode";
      return input_result.error();
    }
    full_resolution_stream_ = std::move(input_result.value());

    auto graph_result = CreateGraph(info, full_resolution_stream_.get());
    if (graph_result.is_error()) {
      FX_PLOGS(ERROR, graph_result.error()) << "Failed to CreateGraph";
      return graph_result.error();
    }

    auto status = graph_result.value()->client_stream()->Attach(stream.TakeChannel(), [this]() {
      FX_LOGS(INFO) << "Stream client disconnected";
      full_resolution_stream_ = nullptr;
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
