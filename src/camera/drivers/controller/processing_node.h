// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <lib/async/cpp/task.h>
#include <zircon/assert.h>

#include <queue>
#include <vector>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>
#include <fbl/macros.h>

#include "src/camera/drivers/controller/configs/internal_config.h"
#include "src/lib/fxl/synchronization/thread_checker.h"

namespace camera {

class ProcessNode;
class StreamImpl;

class ProcessNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ProcessNode);
  ProcessNode(NodeType type, ProcessNode* parent_node,
              fuchsia::camera2::CameraStreamType current_stream_type,
              const std::vector<fuchsia::sysmem::ImageFormat_2>& output_image_formats,
              fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
              const std::vector<StreamInfo>& supported_streams, async_dispatcher_t* dispatcher,
              fuchsia::camera2::FrameRate frame_rate, uint32_t current_image_format_index)
      : dispatcher_(dispatcher),
        output_frame_rate_(frame_rate),
        type_(type),
        parent_node_(parent_node),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        enabled_(false),
        supported_streams_(supported_streams),
        in_use_buffer_count_(output_buffer_collection.buffer_count, 0),
        current_image_format_index_(current_image_format_index) {
    configured_streams_.push_back(current_stream_type);
  }

  virtual ~ProcessNode() {
    // We need to ensure that the child nodes
    // are destructed before parent node.
    child_nodes_.clear();
  }

  // Notifies that a frame is ready for processing at this node.
  virtual void OnReadyToProcess(const frame_available_info_t* info) = 0;

  // Notifies that a frame is released.
  virtual void OnReleaseFrame(uint32_t buffer_index) = 0;

  // Notifies that the client has requested to start streaming.
  virtual void OnStartStreaming();

  // Notifies that the client has requested to stop streaming.
  virtual void OnStopStreaming();

  // Shut down routine.
  virtual void OnShutdown(fit::function<void(void)> shutdown_callback) = 0;

  // Notifies that the client has requested to change resolution.
  virtual void OnResolutionChangeRequest(uint32_t output_format_index) = 0;

  // Notifies that the client has requested a new crop rectangle.
  virtual zx_status_t OnSetCropRect(float /*x_min*/, float /*y_min*/, float /*x_max*/,
                                    float /*y_max*/) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  // Notifies that the resolution has been changed.
  void OnResolutionChanged(const frame_available_info* info);

  void RunOnMainThread(fit::closure handler) { async::PostTask(dispatcher_, std::move(handler)); }

  void set_enabled(bool enabled) { enabled_ = enabled; }

  // Updates the frame counter for all child nodes.
  void UpdateFrameCounterForAllChildren();

  // Decides if we need to drop the frame.
  bool NeedToDropFrame();

  NodeType type() { return type_; }

  std::vector<fuchsia::sysmem::ImageFormat_2>& output_image_formats() {
    return output_image_formats_;
  }

  fuchsia::sysmem::BufferCollectionInfo_2& output_buffer_collection() {
    return output_buffer_collection_;
  }

  uint32_t current_image_format_index() const { return current_image_format_index_; }

  ProcessNode* parent_node() { return parent_node_; }

  uint32_t output_fps() const {
    ZX_ASSERT_MSG(output_frame_rate_.frames_per_sec_numerator %
                          output_frame_rate_.frames_per_sec_denominator ==
                      0,
                  "Unsupported Frame Rate");

    return output_frame_rate_.frames_per_sec_numerator /
           output_frame_rate_.frames_per_sec_denominator;
  }

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

  bool is_stream_supported(fuchsia::camera2::CameraStreamType stream) {
    return std::any_of(
        supported_streams_.begin(), supported_streams_.end(),
        [stream](auto& supported_stream) { return supported_stream.type == stream; });
  }

  bool is_dynamic_resolution_supported(fuchsia::camera2::CameraStreamType stream) {
    for (auto& supported_stream : supported_streams_) {
      if (supported_stream.type == stream) {
        return supported_stream.supports_dynamic_resolution;
      }
    }
    return false;
  }

  bool is_crop_region_supported(fuchsia::camera2::CameraStreamType stream) {
    for (auto& supported_stream : supported_streams_) {
      if (supported_stream.type == stream) {
        return supported_stream.supports_crop_region;
      }
    }
    return false;
  }

  const std::vector<StreamInfo>& supported_streams() { return supported_streams_; }

  std::vector<std::unique_ptr<ProcessNode>>& child_nodes() { return child_nodes_; }

  // Adds a child node in the vector.
  void AddChildNodeInfo(std::unique_ptr<ProcessNode> child_node) {
    child_nodes_.push_back(std::move(child_node));
  }

  void set_current_image_format_index(uint32_t index) { current_image_format_index_ = index; }

  // Curent state of the node.
  bool enabled() const { return enabled_; }

  uint32_t current_frame_count() const { return current_frame_count_; }

  void AddToCurrentFrameCount(uint32_t frame_count) { current_frame_count_ += frame_count; }
  void SubtractFromCurrentFrameCount(uint32_t frame_count) { current_frame_count_ -= frame_count; }

  async_dispatcher_t* dispatcher() const { return dispatcher_; }

 protected:
  bool AllChildNodesDisabled();

  void OnCallbackReceived() {
    if (node_callback_received_ && child_node_callback_received_) {
      shutdown_callback_();
    }
  }

  // Notifies that a frame is done processing by this node.
  virtual void OnFrameAvailable(const frame_available_info_t* info);

  // Dispatcher for the frame processng loop.
  async_dispatcher_t* dispatcher_;
  // Lock to guard |in_use_buffer_count_|
  fbl::Mutex in_use_buffer_lock_;
  // The output frame rate for this node.
  fuchsia::camera2::FrameRate output_frame_rate_;
  // Current frame counter. This is in terms of no. of output frames
  // worth of input recieved.
  // current_frame_count = (no. of input frames generated * output_frame_rate)
  uint32_t current_frame_count_ = 0;
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
  std::vector<StreamInfo> supported_streams_;
  // A vector to keep track of outstanding in-use buffers handed off to all child nodes.
  // [buffer_index] --> [count]
  std::vector<uint32_t> in_use_buffer_count_ __TA_GUARDED(in_use_buffer_lock_);
  // ISP/GDC or GE2D shutdown complete status.
  bool node_callback_received_ = false;
  // Child node shutdown complete status.
  bool child_node_callback_received_ = false;
  fit::function<void(void)> shutdown_callback_;
  bool shutdown_requested_ = false;
  uint32_t current_image_format_index_;
  fxl::ThreadChecker thread_checker_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
