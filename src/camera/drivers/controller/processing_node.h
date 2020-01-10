// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <zircon/assert.h>

#include <queue>
#include <vector>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>

#include "fbl/macros.h"
#include "src/camera/drivers/controller/configs/sherlock/internal_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"

namespace camera {

class ProcessNode;
class StreamImpl;

class ProcessNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ProcessNode);
  ProcessNode(NodeType type, std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
              fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
              fuchsia::camera2::CameraStreamType current_stream_type,
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams,
              async_dispatcher_t* dispatcher, fuchsia::camera2::FrameRate frame_rate)
      : dispatcher_(dispatcher),
        output_frame_rate_(frame_rate),
        type_(type),
        parent_node_(nullptr),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        enabled_(false),
        supported_streams_(supported_streams),
        in_use_buffer_count_(output_buffer_collection.buffer_count, 0) {
    ZX_ASSERT(type == NodeType::kInputStream);
    configured_streams_.push_back(current_stream_type);
  }

  ProcessNode(NodeType type, ProcessNode* parent_node,
              fuchsia::camera2::CameraStreamType current_stream_type,
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams,
              async_dispatcher_t* dispatcher, fuchsia::camera2::FrameRate frame_rate)
      : dispatcher_(dispatcher),
        output_frame_rate_(frame_rate),
        type_(type),
        parent_node_(parent_node),
        enabled_(false),
        supported_streams_(supported_streams) {
    ZX_ASSERT(type == NodeType::kOutputStream);
    ZX_ASSERT(parent_node_ != nullptr);
    configured_streams_.push_back(current_stream_type);
  }

  ProcessNode(const ddk::GdcProtocolClient& gdc, NodeType type, ProcessNode* parent_node,
              std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
              fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
              fuchsia::camera2::CameraStreamType current_stream_type,
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams,
              async_dispatcher_t* dispatcher, fuchsia::camera2::FrameRate frame_rate)
      : dispatcher_(dispatcher),
        output_frame_rate_(frame_rate),
        type_(type),
        parent_node_(parent_node),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        enabled_(false),
        supported_streams_(supported_streams),
        in_use_buffer_count_(output_buffer_collection.buffer_count, 0) {
    ZX_ASSERT(type == NodeType::kGdc);
    configured_streams_.push_back(current_stream_type);
  }

  virtual ~ProcessNode() {
    // We need to ensure that the child nodes
    // are destructed before parent node.
    child_nodes_.clear();
  }

  // Notifies that a frame is ready for processing at this node.
  virtual void OnReadyToProcess(uint32_t buffer_index) = 0;

  // Notifies that a frame is done processing by this node.
  virtual void OnFrameAvailable(const frame_available_info_t* info);

  // Notifies that a frame is released.
  virtual void OnReleaseFrame(uint32_t buffer_index) = 0;

  // Notifies that the client has requested to start streaming.
  virtual void OnStartStreaming();

  // Notifies that the client has requested to stop streaming.
  virtual void OnStopStreaming();

  // Shut down routine.
  virtual void OnShutdown() = 0;

  void set_enabled(bool enabled) { enabled_ = enabled; }

  NodeType type() { return type_; }

  std::vector<fuchsia::sysmem::ImageFormat_2>& output_image_formats() {
    return output_image_formats_;
  }

  fuchsia::sysmem::BufferCollectionInfo_2& output_buffer_collection() {
    return output_buffer_collection_;
  }

  ProcessNode* parent_node() { return parent_node_; }

  std::vector<fuchsia::camera2::CameraStreamType>& configured_streams() {
    return configured_streams_;
  }

  // For tests.
  uint32_t get_in_use_buffer_count(uint32_t buffer_index) {
    {
      fbl::AutoLock al(&in_use_buffer_lock_);
      ZX_ASSERT(buffer_index < in_use_buffer_count_.size());
      return in_use_buffer_count_[buffer_index];
    }
  }

  std::vector<fuchsia::camera2::CameraStreamType> supported_streams() { return supported_streams_; }

  std::vector<std::unique_ptr<ProcessNode>>& child_nodes() { return child_nodes_; }

  // Adds a child node in the vector.
  void AddChildNodeInfo(std::unique_ptr<ProcessNode> child_node) {
    child_nodes_.push_back(std::move(child_node));
  }
  // Curent state of the node.
  bool enabled() { return enabled_; }

 protected:
  bool AllChildNodesDisabled();
  // Dispatcher for the frame processng loop.
  async_dispatcher_t* dispatcher_;
  // Lock to guard |in_use_buffer_count_|
  fbl::Mutex in_use_buffer_lock_;
  // Lock to guard |event_queue_|.
  fbl::Mutex event_queue_lock_;
  // The output frame rate for this node.
  fuchsia::camera2::FrameRate output_frame_rate_;
  // Type of node.
  NodeType type_;
  // List of all the children for this node.
  std::vector<std::unique_ptr<ProcessNode>> child_nodes_;
  // Parent node.
  ProcessNode* const parent_node_;
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection_;
  // Ouput Image formats.
  // These are needed when we initialize HW accelerators.
  std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats_;
  bool enabled_;
  // The Stream types this node already supports and configured.
  std::vector<fuchsia::camera2::CameraStreamType> configured_streams_;
  // The Stream types this node could support as well.
  std::vector<fuchsia::camera2::CameraStreamType> supported_streams_;
  // A vector to keep track of outstanding in-use buffers handed off to all child nodes.
  // [buffer_index] --> [count]
  std::vector<uint32_t> in_use_buffer_count_ __TA_GUARDED(in_use_buffer_lock_);
  // Task queue for all the frame processing.
  std::queue<async::TaskClosure> event_queue_ __TA_GUARDED(event_queue_lock_);
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
