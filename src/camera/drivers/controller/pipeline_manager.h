// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include "fbl/macros.h"
#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/drivers/controller/gdc_node.h"
#include "src/camera/drivers/controller/input_node.h"
#include "src/camera/drivers/controller/output_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"

namespace camera {

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

  zx_status_t ConfigureStreamPipeline(StreamCreationData* info,
                                      fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Creates the stream pipeline graph and appends it to the input node (|parent_node|).
  // Args:
  // |internal_node| : Internal node of the node where this new graph needs to append.
  // |parent_node| : Pointer to the node to which  we need to append this new graph.
  // Returns:
  // |OutputNode*| : Pointer to the ouput node.
  fit::result<OutputNode*, zx_status_t> CreateGraph(StreamCreationData* info,
                                                    const InternalConfigNode& internal_node,
                                                    ProcessNode* parent_node);

  zx_status_t AppendToExistingGraph(StreamCreationData* info, ProcessNode* graph_node,
                                    fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Disconnects the stream.
  // This is called when the stream channel receives a ZX_ERR_PEER_CLOSE message.
  // |stream_to_be_disconnected| : Stream type of the stream to be disconnected.
  // |input_stream_type|         : Either FR or DS.
  void OnClientStreamDisconnect(fuchsia::camera2::CameraStreamType input_stream_type,
                                fuchsia::camera2::CameraStreamType stream_to_be_disconnected);

  // Helper function to find out which portion of the graph
  // needs to be disconnected and shut down.
  void DisconnectStream(ProcessNode* graph_head,
                        fuchsia::camera2::CameraStreamType input_stream_type,
                        fuchsia::camera2::CameraStreamType stream_to_disconnect);

  // Frees up the nodes after the stream pipeline has been shutdown
  // when |stream_to_disconnect| stream is disconnected.
  // After a stream has shutdown, we have to check again to see what part of the
  // graph needs to be freed up because there is a possibility where while a portion
  // of graph is waiting to be shut down, another request for disconnection came in for
  // same |input_stream_type|.
  void DeleteGraphForDisconnectedStream(ProcessNode* graph_head,
                                        fuchsia::camera2::CameraStreamType stream_to_disconnect);

  fit::result<std::pair<InternalConfigNode, ProcessNode*>, zx_status_t> FindNodeToAttachNewStream(
      StreamCreationData* info, const InternalConfigNode& current_internal_node,
      ProcessNode* graph_head);

  ProcessNode* full_resolution_stream() { return full_resolution_stream_.get(); }
  ProcessNode* downscaled_resolution_stream() { return downscaled_resolution_stream_.get(); }

 private:
  fit::result<std::unique_ptr<InputNode>, zx_status_t> ConfigureStreamPipelineHelper(
      StreamCreationData* info, fidl::InterfaceRequest<fuchsia::camera2::Stream>& stream);

  // Lock to guard |event_queue_|.
  fbl::Mutex event_queue_lock_;

  zx_device_t* device_;
  async_dispatcher_t* dispatcher_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ControllerMemoryAllocator memory_allocator_;
  std::unique_ptr<ProcessNode> full_resolution_stream_;
  std::unique_ptr<ProcessNode> downscaled_resolution_stream_;
  std::queue<async::TaskClosure> event_queue_ __TA_GUARDED(event_queue_lock_);
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
