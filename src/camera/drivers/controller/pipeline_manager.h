// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/async/cpp/wait.h>

#include <unordered_map>
#include <vector>

#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/output_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"

namespace camera {

namespace {
constexpr auto kPipelineManagerSignalExitDone = ZX_USER_SIGNAL_0;
constexpr auto kTaskQueued = ZX_USER_SIGNAL_0;
}  // namespace

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
                  const ddk::Ge2dProtocolClient& ge2d,
                  fuchsia::sysmem::AllocatorSyncPtr sysmem_allocator,
                  const zx::event& shutdown_event)
      : shutdown_event_(shutdown_event),
        device_(device),
        dispatcher_(dispatcher),
        isp_(isp),
        gdc_(gdc),
        ge2d_(ge2d),
        memory_allocator_(std::move(sysmem_allocator)) {
    SetupTaskWaiter();
  }

  void ConfigureStreamPipeline(StreamCreationData info,
                               fidl::InterfaceRequest<fuchsia::camera2::Stream> stream);

  // Disconnects the stream.
  // This is called when the stream channel receives a ZX_ERR_PEER_CLOSE message.
  // |stream_to_be_disconnected| : Stream type of the stream to be disconnected.
  void OnClientStreamDisconnect(fuchsia::camera2::CameraStreamType stream_to_be_disconnected);

  ProcessNode* full_resolution_stream() const {
    return FindStream(fuchsia::camera2::CameraStreamType::FULL_RESOLUTION);
  }

  ProcessNode* downscaled_resolution_stream() const {
    return FindStream(fuchsia::camera2::CameraStreamType::DOWNSCALED_RESOLUTION);
  }

  void StopStreaming();
  void StartStreaming();

  // Shuts down all existing streams
  void Shutdown();

  // Finds which graph head is the requested stream |stream_type| configured in.
  fit::result<std::pair<ProcessNode*, fuchsia::camera2::CameraStreamType>, zx_status_t>
  FindGraphHead(fuchsia::camera2::CameraStreamType stream_type);

 private:
  // Creates an async wait object which waits on |tasks_event_| to check the task queue and drains
  // it.
  void SetupTaskWaiter();

  // Posts a task on the task queue. All the tasks related to the manipulation of the stream
  // configurations need to be posted on the task queue instead of directly posting them on the loop
  // using async::PostTask(). Note: The task posted to the queue needs to call TaskComplete() to
  // signal completion of the task.
  void PostTask(fit::closure task);

  // Signals the completion of the task.
  void TaskComplete();

  // Frees up the nodes after the stream pipeline has been shutdown
  // when |stream_to_disconnect| stream is disconnected.
  // After a stream has shutdown, we have to check again to see what part of the
  // graph needs to be freed up because there is a possibility where while a portion
  // of graph is waiting to be shut down, another request for disconnection came in for
  // same |input_stream_type|.
  void DeleteGraphForDisconnectedStream(ProcessNode* graph_head,
                                        fuchsia::camera2::CameraStreamType stream_to_disconnect);

  // Creates the stream pipeline graph and appends it to the input node (|parent_node|).
  // Args:
  // |internal_node| : Internal node of the node where this new graph needs to append.
  // |parent_node| : Pointer to the node to which  we need to append this new graph.
  // Returns:
  // |OutputNode*| : Pointer to the ouput node.
  fit::result<OutputNode*, zx_status_t> CreateGraph(StreamCreationData* info,
                                                    const InternalConfigNode& internal_node,
                                                    ProcessNode* parent_node);

  fit::result<std::pair<InternalConfigNode, ProcessNode*>, zx_status_t> FindNodeToAttachNewStream(
      StreamCreationData* info, const InternalConfigNode& current_internal_node,
      ProcessNode* graph_head);

  // Helper function to find out which portion of the graph
  // needs to be disconnected and shut down.
  void DisconnectStream(ProcessNode* graph_head,
                        fuchsia::camera2::CameraStreamType input_stream_type,
                        fuchsia::camera2::CameraStreamType stream_to_disconnect);

  ProcessNode* FindStream(fuchsia::camera2::CameraStreamType stream) const {
    auto stream_entry = streams_.find(stream);
    if (stream_entry != streams_.end()) {
      return stream_entry->second.get();
    }
    return nullptr;
  }

  bool global_shutdown_requested_ = false;
  const zx::event& shutdown_event_;
  zx_device_t* device_;
  async_dispatcher_t* dispatcher_;
  ddk::IspProtocolClient isp_;
  ddk::GdcProtocolClient gdc_;
  ddk::Ge2dProtocolClient ge2d_;
  ControllerMemoryAllocator memory_allocator_;
  // Map of Input streams -> ProcessNodes
  std::unordered_map<fuchsia::camera2::CameraStreamType, std::unique_ptr<ProcessNode>> streams_;
  // Map of Output streams -> OutputNodes
  std::unordered_map<fuchsia::camera2::CameraStreamType, OutputNode*> output_nodes_info_;
  std::vector<fuchsia::camera2::CameraStreamType> stream_shutdown_requested_;
  // Queue for stream creation & deletion tasks.
  std::queue<fit::closure> task_queue_;
  bool task_in_progress_ = false;
  zx::event tasks_event_;
  async::Wait tasks_event_waiter_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PIPELINE_MANAGER_H_
