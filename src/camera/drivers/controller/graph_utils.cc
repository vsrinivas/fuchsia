// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "graph_utils.h"

#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/lib/syslog/cpp/logger.h"

namespace camera {

constexpr auto kTag = "camera_controller_graph_helper";

const InternalConfigNode* GetNextNodeInPipeline(const fuchsia::camera2::CameraStreamType& stream,
                                                const InternalConfigNode& node) {
  for (const auto& child_node : node.child_nodes) {
    for (uint32_t i = 0; i < child_node.supported_streams.size(); i++) {
      if (child_node.supported_streams[i] == stream) {
        return &child_node;
      }
    }
  }
  return nullptr;
}

fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> GetBuffers(
    const ControllerMemoryAllocator& memory_allocator, const InternalConfigNode& producer,
    StreamCreationData* info, ProcessNode* producer_graph_node) {
  fuchsia::sysmem::BufferCollectionInfo_2 buffers;
  auto consumer = GetNextNodeInPipeline(info->stream_config->properties.stream_type(), producer);
  if (!consumer) {
    FX_LOGST(ERROR, kTag) << "Failed to get next node";
    return fit::error(ZX_ERR_INTERNAL);
  }

  // If the consumer is the client, we use the client buffers
  if (consumer->type == kOutputStream) {
    return fit::ok(std::move(info->output_buffers));
  }

  // The controller will need to allocate memory using sysmem.
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  for (const auto& node : producer.child_nodes) {
    if (node.type != kOutputStream) {
      constraints.push_back(node.input_constraints);
    }
  }
  constraints.push_back(producer.output_constraints);

  auto status = memory_allocator.AllocateSharedMemory(constraints, &buffers);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate shared memory";
    return fit::error(status);
  }
  return fit::ok(std::move(buffers));
}

}  // namespace camera
