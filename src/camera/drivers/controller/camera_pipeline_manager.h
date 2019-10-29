// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CAMERA_PIPELINE_MANAGER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CAMERA_PIPELINE_MANAGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include "configs/sherlock/internal-config.h"
#include "controller-processing-node.h"
#include "fbl/macros.h"
namespace camera {
struct CameraPipelineInfo {
  InternalConfigNode node;
  const fuchsia::camera2::hal::StreamConfig* stream_config;
  uint32_t image_format_index;
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffers;
};

// |CameraPipelineManager|
// This class provides a way to create the stream pipeline for a particular
// stream configuration requested.
// While doing so it would also create ISP stream protocol and client stream protocols
// and setup the camera pipeline such that the streams are flowing properly as per the
// requested stream configuration.
class CameraPipelineManager {
 public:
  CameraPipelineManager(async_dispatcher_t* dispatcher, ddk::IspProtocolClient& isp,
                        fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : dispatcher_(dispatcher), isp_(isp), memory_allocator_(std::move(sysmem_allocator)) {}

  // For tests.
  CameraPipelineManager(ddk::IspProtocolClient& isp,
                        fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : isp_(isp), memory_allocator_(std::move(sysmem_allocator)) {}

  zx_status_t ConfigureStreamPipeline(CameraPipelineInfo* info,
                                      fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Configures the input node: Does the following things
  // 1. Creates the ISP stream protocol
  // 2. Creates the requested ISP stream
  // 3. Allocate buffers if needed
  // 4. Creates the CameraProcessNode for the input node
  zx_status_t ConfigureInputNode(CameraPipelineInfo* info,
                                 std::shared_ptr<CameraProcessNode>* out_processing_node);

  zx_status_t ConfigureOutputNode(const std::shared_ptr<CameraProcessNode>& parent_node,
                                  const InternalConfigNode* node,
                                  std::shared_ptr<CameraProcessNode>* output_processing_node);
  // Create the stream pipeline graph
  zx_status_t CreateGraph(CameraPipelineInfo* info,
                          const std::shared_ptr<CameraProcessNode>& parent_node,
                          std::shared_ptr<CameraProcessNode>* output_processing_node);
  // Gets the next node for the requested stream path
  const InternalConfigNode* GetNextNodeInPipeline(CameraPipelineInfo* info,
                                                  const InternalConfigNode& node);

  // Gets the right buffercollection for the producer-consumer combination
  zx_status_t GetBuffers(const InternalConfigNode& producer, CameraPipelineInfo* info,
                         fuchsia::sysmem::BufferCollectionInfo_2* out_buffers);

 private:
  async_dispatcher_t* dispatcher_;
  ddk::IspProtocolClient& isp_;
  ControllerMemoryAllocator memory_allocator_;
  std::shared_ptr<CameraProcessNode> full_resolution_stream_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CAMERA_PIPELINE_MANAGER_H_
