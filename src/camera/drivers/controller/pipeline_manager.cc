// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "pipeline_manager.h"

#include <zircon/errors.h>
#include <zircon/types.h>

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

// NOTE: This API currently supports only debug config
// At a later point it will also need to take care of scenarios where same source stream
// provides multiple output streams.
fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> PipelineManager::GetBuffers(
    const InternalConfigNode& producer, PipelineInfo* info) {
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  auto consumer = GetNextNodeInPipeline(info, producer);
  if (!consumer) {
    FX_LOGS(ERROR) << "Failed to get next node";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // If the consumer is the client, we use the client buffers
  if (consumer->type == kOutputStream) {
    buffers = std::move(info->output_buffers);
  } else {
    // We need to allocate memory using sysmem
    // TODO(braval): Add support for the case of two consumer nodes, which will be needed for the
    // video conferencing config.
    std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
    constraints.push_back(producer.constraints);
    constraints.push_back(consumer->constraints);

    auto status = memory_allocator_.AllocateSharedMemory(constraints, &buffers);
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "Failed to allocate shared memory";
      return fit::error(status);
    }
  }

  return fit::ok(std::move(buffers));
}

fit::result<std::unique_ptr<ProcessNode>, zx_status_t> PipelineManager::CreateInputNode(
    PipelineInfo* info) {
  uint8_t isp_stream_type;
  if (info->node.input_stream_type == fuchsia::camera2::CameraStreamType::FULL_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_FULL_RESOLUTION;
  } else if (info->node.input_stream_type ==
             fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION) {
    isp_stream_type = STREAM_TYPE_DOWNSCALED;
  } else {
    return fit::error(ZX_ERR_INVALID_ARGS);
  }

  auto result = GetBuffers(info->node, info);
  if (result.is_error()) {
    FX_PLOGS(ERROR, result.error()) << "Failed to get buffers";
    return fit::error(result.error());
  }
  fuchsia::sysmem::BufferCollectionInfo_2 buffers = std::move(result.value());

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
  auto status = isp_.CreateOutputStream(
      &old_buffer_collection, reinterpret_cast<const frame_rate_t*>(&info->node.output_frame_rate),
      isp_stream_type, processing_node->callback(), isp_stream_protocol->protocol());
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

fit::result<std::unique_ptr<ProcessNode>, zx_status_t>
PipelineManager::ConfigureStreamPipelineHelper(
    PipelineInfo* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream) {
  // Configure Input node
  auto input_result = CreateInputNode(info);
  if (input_result.is_error()) {
    FX_PLOGS(ERROR, input_result.error()) << "Failed to ConfigureInputNode";
    return input_result;
  }

  auto input_processing_node = std::move(input_result.value());
  ProcessNode* input_node = input_processing_node.get();

  auto output_node_result = CreateGraph(info, input_node);
  if (output_node_result.is_error()) {
    FX_PLOGS(ERROR, output_node_result.error()) << "Failed to CreateGraph";
    return fit::error(output_node_result.error());
  }

  auto output_node = output_node_result.value();
  auto status = output_node->client_stream()->Attach(stream.TakeChannel(), [this, output_node]() {
    FX_LOGS(INFO) << "Stream client disconnected";
    OnClientStreamDisconnect(output_node);
  });
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to bind output stream";
    return fit::error(status);
  }
  return fit::ok(std::move(input_processing_node));
}

zx_status_t PipelineManager::ConfigureStreamPipeline(
    PipelineInfo* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream) {
  // Input Validations
  if (info == nullptr || info->stream_config == nullptr) {
    return ZX_ERR_INVALID_ARGS;
  }
  // Here at top level we check what type of input stream do we have to deal with
  switch (info->node.input_stream_type) {
    case fuchsia::camera2::CameraStreamType::FULL_RESOLUTION: {
      if (full_resolution_stream_) {
        // TODO(braval): If valid it means we need to modify existing graph
        // TODO(braval): Check if same stream is requested, if so do not allow
        // Currently we will only be not allowing since we only support ISP debug config.
        FX_PLOGS(ERROR, ZX_ERR_ALREADY_BOUND) << "Stream already bound";
        return ZX_ERR_ALREADY_BOUND;
      }
      auto result = ConfigureStreamPipelineHelper(info, stream);
      if (result.is_error()) {
        return result.error();
      }
      full_resolution_stream_ = std::move(result.value());
      break;
    }
    case fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION: {
      if (downscaled_resolution_stream_) {
        // TODO(braval): If valid it means we need to modify existing graph
        // TODO(braval): Check if same stream is requested, if so do not allow
        // Currently we will only be not allowing since we only support ISP debug config.
        FX_PLOGS(ERROR, ZX_ERR_ALREADY_BOUND) << "Stream already bound";
        return ZX_ERR_ALREADY_BOUND;
      }
      auto result = ConfigureStreamPipelineHelper(info, stream);
      if (result.is_error()) {
        return result.error();
      }
      downscaled_resolution_stream_ = std::move(result.value());
      break;
    }
    default: {
      FX_LOGS(ERROR) << "Invalid input stream type";
      return ZX_ERR_INVALID_ARGS;
    }
  }
  return ZX_OK;
}

}  // namespace camera
