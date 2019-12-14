// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <zircon/assert.h>

#include <vector>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/isp.h>
#include <fbl/auto_lock.h>

#include "configs/sherlock/internal-config.h"
#include "fbl/macros.h"
#include "isp_stream_protocol.h"
#include "memory_allocation.h"
#include "stream_protocol.h"
namespace camera {

class ProcessNode;
class StreamImpl;

struct ChildNodeInfo {
  // Pointer to the child node.
  std::unique_ptr<ProcessNode> child_node;
  // The frame rate for this node.
  fuchsia::camera2::FrameRate output_frame_rate;
};

class ProcessNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ProcessNode);
  ProcessNode(NodeType type, std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
              fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
              fuchsia::camera2::CameraStreamType current_stream_type,
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams)
      : type_(type),
        parent_node_(nullptr),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        hw_accelerator_frame_callback_{OnFrameAvailable, this},
        enabled_(false),
        supported_streams_(supported_streams),
        in_use_buffer_count_(output_buffer_collection.buffer_count, 0) {
    ZX_ASSERT(type == NodeType::kInputStream);
    configured_streams_.push_back(current_stream_type);
  }

  explicit ProcessNode(NodeType type, ProcessNode* parent_node,
                       fuchsia::camera2::CameraStreamType current_stream_type,
                       std::vector<fuchsia::camera2::CameraStreamType> supported_streams)
      : type_(type),
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
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams)
      : type_(type),
        parent_node_(parent_node),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        hw_accelerator_frame_callback_{OnFrameAvailable, this},
        hw_accelerator_res_callback_{OnResChange, this},
        gdc_(gdc),
        enabled_(false),
        supported_streams_(supported_streams),
        in_use_buffer_count_(output_buffer_collection.buffer_count, 0) {
    ZX_ASSERT(type == NodeType::kGdc);
    configured_streams_.push_back(current_stream_type);
  }

  ~ProcessNode() {
    // We need to ensure that the child nodes
    // are destructed before parent node.
    OnShutdown();
    child_nodes_info_.clear();
  }

  // Called when input is ready for this processing node.
  // For node type |kOutputStream| we will be just calling the registered
  // callbacks.
  void OnReadyToProcess(uint32_t buffer_index);

  // Callback function when frame is done processing for this node by the HW.
  // Here we would scan the |nodes_| and call each nodes |OnReadyToProcess()|
  void OnFrameAvailable(const frame_available_info_t* info);

  // Called by child nodes when the frame is released.
  // Here we would first free up the frame with the HW and then
  // call parents OnReleaseFrame()
  void OnReleaseFrame(uint32_t buffer_index);

  // Called by client
  void OnStartStreaming();
  void OnStopStreaming();

  // This API gets called when a client disconnects the stream.
  void OnShutdown();

  // Helper APIs
  void set_isp_stream_protocol(std::unique_ptr<camera::IspStreamProtocol> isp_stream_protocol) {
    isp_stream_protocol_ = std::move(isp_stream_protocol);
  }
  void set_client_stream(std::unique_ptr<StreamImpl> client_stream) {
    client_stream_ = std::move(client_stream);
  }
  void set_task_index(uint32_t task_index) { hw_accelerator_task_index_ = task_index; }
  void set_enabled(bool enabled) { enabled_ = enabled; }

  std::unique_ptr<camera::StreamImpl>& client_stream() { return client_stream_; }
  std::unique_ptr<camera::IspStreamProtocol>& isp_stream_protocol() { return isp_stream_protocol_; }
  NodeType type() { return type_; }

  const hw_accel_frame_callback_t* hw_accelerator_frame_callback() {
    return &hw_accelerator_frame_callback_;
  }
  const hw_accel_res_change_callback_t* hw_accelerator_res_callback() {
    return &hw_accelerator_res_callback_;
  }

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

  std::vector<ChildNodeInfo>& child_nodes_info() { return child_nodes_info_; }

  // Adds a child info in the vector
  void AddChildNodeInfo(ChildNodeInfo info) { child_nodes_info_.push_back(std::move(info)); }
  // Curent state of the node
  bool enabled() { return enabled_; }

 private:
  // Invoked by GDC, GE2D, or the ISP when a new frame is available.
  static void OnFrameAvailable(void* ctx, const frame_available_info_t* info) {
    static_cast<ProcessNode*>(ctx)->OnFrameAvailable(info);
  }
  // Invoked by GDC or GE2D on a Resolution change completion.
  // TODO: Implement this (Bug: 41730 @braval).
  static void OnResChange(void* ctx, const frame_available_info_t* info) {}

  bool AllChildNodesDisabled();

  // Lock to guard |in_use_buffer_count_|
  fbl::Mutex in_use_buffer_lock_;

  // Type of node.
  NodeType type_;
  // List of all the children for this node.
  std::vector<ChildNodeInfo> child_nodes_info_;
  // Parent node
  ProcessNode* const parent_node_;
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection_;
  // Ouput Image formats
  // These are needed when we initialize HW accelerators.
  std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats_;
  // Valid for output node
  std::unique_ptr<StreamImpl> client_stream_;
  // Valid for input node
  std::unique_ptr<IspStreamProtocol> isp_stream_protocol_;
  // GDC/GE2D/ISP Frame callback
  hw_accel_frame_callback_t hw_accelerator_frame_callback_;
  // GDC/GE2D Res change callback
  hw_accel_res_change_callback_t hw_accelerator_res_callback_;
  ddk::GdcProtocolClient gdc_;
  uint32_t hw_accelerator_task_index_;
  bool enabled_;
  // The Stream types this node already supports and configured.
  std::vector<fuchsia::camera2::CameraStreamType> configured_streams_;
  // The Stream types this node could support as well.
  std::vector<fuchsia::camera2::CameraStreamType> supported_streams_;
  // A vector to keep track of outstanding in-use buffers handed off to all child nodes.
  // [buffer_index] --> [count]
  std::vector<uint32_t> in_use_buffer_count_ __TA_GUARDED(in_use_buffer_lock_);
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
