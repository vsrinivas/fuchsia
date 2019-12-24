// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GRAPH_UTILS_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GRAPH_UTILS_H_

#include <vector>

#include "configs/sherlock/internal-config.h"
#include "fbl/macros.h"
#include "processing_node.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"
#include "stream_pipeline_info.h"

namespace camera {

// Gets the next node for the requested stream path
const InternalConfigNode* GetNextNodeInPipeline(const fuchsia::camera2::CameraStreamType& stream,
                                                const InternalConfigNode& node);

// NOTE: This API currently supports only single consumer node use cases.
// Gets the right buffercollection for the producer-consumer combination.
// |memory_allocator| - Memory allocator to allocate memory using sysmem.
// |producer| - Internal node for the producer.
// |info| - Info about the stream to be created & the client buffer collection.
// |producer_graph_node| - If this is nullptr, this API would allocate new buffers
//                         If this is a valid node, then we would use the output
//                         buffer collection of that node. This is needed for streams
//                         which have one parent.
fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> GetBuffers(
    const ControllerMemoryAllocator& memory_allocator, const InternalConfigNode& producer,
    StreamCreationData* info, ProcessNode* producer_graph_node);

// Returns |true| if CameraStreamType |type| is present in the
// vector |streams|.
inline bool HasStreamType(const std::vector<fuchsia::camera2::CameraStreamType>& streams,
                          fuchsia::camera2::CameraStreamType type) {
  return std::find(streams.begin(), streams.end(), type) != streams.end();
}

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_GRAPH_UTILS_H_
