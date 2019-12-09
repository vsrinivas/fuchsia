// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include "configs/sherlock/internal-config.h"
#include "fbl/macros.h"
#include "processing_node.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"

namespace camera {
struct PipelineInfo {
  InternalConfigNode node;
  const fuchsia::camera2::hal::StreamConfig* stream_config;
  uint32_t image_format_index;
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffers;
};

// |PipelineManager|
// This class provides a way to create the stream pipeline for a particular
// stream configuration requested.
// While doing so it would also create ISP stream protocol and client stream protocols
// and setup the camera pipeline such that the streams are flowing properly as per the
// requested stream configuration.
class PipelineManager {
 public:
  PipelineManager(zx_device_t* device, async_dispatcher_t* dispatcher,
                  const ddk::IspProtocolClient& isp, const ddk::GdcProtocolClient& gdc,
                  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : device_(device),
        dispatcher_(dispatcher),
        isp_(isp),
        gdc_(gdc),
        memory_allocator_(std::move(sysmem_allocator)) {}

  // For tests.
  PipelineManager(zx_device_t* device, const ddk::IspProtocolClient& isp,
                  const ddk::GdcProtocolClient& gdc,
                  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator)
      : device_(device), isp_(isp), gdc_(gdc), memory_allocator_(std::move(sysmem_allocator)) {}

  zx_status_t ConfigureStreamPipeline(PipelineInfo* info,
                                      fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Configures the input node: Does the following things
  // 1. Creates the ISP stream protocol
  // 2. Creates the requested ISP stream
  // 3. Allocate buffers if needed
  // 4. Creates the ProcessNode for the input node
  fit::result<std::unique_ptr<ProcessNode>, zx_status_t> CreateInputNode(PipelineInfo* info);

  fit::result<ProcessNode*, zx_status_t> CreateOutputNode(
      PipelineInfo* info, ProcessNode* parent_node, const InternalConfigNode& internal_output_node);

  fit::result<ProcessNode*, zx_status_t> CreateGdcNode(PipelineInfo* info, ProcessNode* parent_node,
                                                       const InternalConfigNode& internal_gdc_node);

  // Create the stream pipeline graph
  fit::result<ProcessNode*, zx_status_t> CreateGraph(PipelineInfo* info,
                                                     const InternalConfigNode& internal_node,
                                                     ProcessNode* parent_node);

  zx_status_t AppendToExistingGraph(PipelineInfo* info, ProcessNode* graph_node,
                                    fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Gets the next node for the requested stream path
  const InternalConfigNode* GetNextNodeInPipeline(PipelineInfo* info,
                                                  const InternalConfigNode& node);

  // Gets the right buffercollection for the producer-consumer combination.
  // |producer| - Internal node for the producer.
  // |info| - Info about the stream to be created & the client buffer collection.
  // |producer_graph_node| - If this is nullptr, this API would allocate new buffers
  //                         If this is a valid node, then we would use the output
  //                         buffer collection of that node. This is needed for streams
  //                         which have one parent.
  fit::result<fuchsia::sysmem::BufferCollectionInfo_2, zx_status_t> GetBuffers(
      const InternalConfigNode& producer, PipelineInfo* info, ProcessNode* producer_graph_node);

  fit::result<gdc_config_info, zx_status_t> LoadGdcConfiguration(
      const camera::GdcConfig& config_type);

  void OnClientStreamDisconnect(PipelineInfo* info);

  bool IsStreamAlreadyCreated(PipelineInfo* info, ProcessNode* node);

  fit::result<std::pair<InternalConfigNode, ProcessNode*>, zx_status_t> FindNodeToAttachNewStream(
      PipelineInfo* info, const InternalConfigNode& current_internal_node, ProcessNode* graph_head);

 private:
  fit::result<std::unique_ptr<ProcessNode>, zx_status_t> ConfigureStreamPipelineHelper(
      PipelineInfo* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  zx_device_t* device_;
  async_dispatcher_t* dispatcher_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ControllerMemoryAllocator memory_allocator_;
  std::unique_ptr<ProcessNode> full_resolution_stream_;
  std::unique_ptr<ProcessNode> downscaled_resolution_stream_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
