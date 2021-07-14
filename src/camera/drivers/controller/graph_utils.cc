// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/camera/drivers/controller/graph_utils.h"

#include <lib/fpromise/result.h>
#include <lib/syslog/cpp/macros.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/lib/fsl/handles/object_info.h"

namespace camera {

constexpr auto kTag = "camera_controller_graph_helper";

const InternalConfigNode* GetNextNodeInPipeline(const fuchsia::camera2::CameraStreamType& stream,
                                                const InternalConfigNode& node) {
  for (const auto& child_node : node.child_nodes) {
    for (uint32_t i = 0; i < child_node.supported_streams.size(); i++) {
      if (child_node.supported_streams[i].type == stream) {
        return &child_node;
      }
    }
  }
  return nullptr;
}

fpromise::result<BufferCollection, zx_status_t> GetBuffers(
    const ControllerMemoryAllocator& memory_allocator, const InternalConfigNode& producer,
    StreamCreationData* info, const std::string& buffer_tag) {
  BufferCollection collection;
  const auto* consumer = GetNextNodeInPipeline(info->stream_type(), producer);
  auto current_producer = &producer;

  if (!consumer) {
    FX_LOGST(ERROR, kTag) << "Failed to get next node";
    return fpromise::error(ZX_ERR_INTERNAL);
  }

  // The controller might need to allocate memory using sysmem.
  // Populate the constraints.
  std::vector<fuchsia::sysmem::BufferCollectionConstraints> constraints;
  constraints.push_back(producer.output_constraints);

  while (consumer->in_place) {
    if (current_producer->child_nodes.size() != 1) {
      FX_LOGST(ERROR, kTag)
          << "Invalid configuration. A buffer is shared with a node which does in place operations";
      return fpromise::error(ZX_ERR_BAD_STATE);
    }
    constraints.push_back(consumer->input_constraints);
    current_producer = consumer;
    consumer = &consumer->child_nodes[0];
  }

  for (const auto& node : current_producer->child_nodes) {
    if (node.type != kOutputStream) {
      constraints.push_back(node.input_constraints);
    }
  }

  auto status = memory_allocator.AllocateSharedMemory(constraints, collection, buffer_tag);
  if (status != ZX_OK) {
    FX_PLOGS(ERROR, status) << "Failed to allocate shared memory";
    return fpromise::error(status);
  }
  FX_LOGST(DEBUG, kTag) << "Allocated " << collection.buffers.buffer_count << " buffers for "
                        << buffer_tag;

  return fpromise::ok(std::move(collection));
}

}  // namespace camera
