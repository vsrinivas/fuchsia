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
  std::shared_ptr<ProcessNode> child_node;
  // The frame rate for this node.
  fuchsia::camera2::FrameRate output_frame_rate;
};

class ProcessNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(ProcessNode);
  ProcessNode(NodeType type, std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
              fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
              fuchsia_sysmem_BufferCollectionInfo old_output_buffer_collection,
              fuchsia::camera2::CameraStreamType current_stream_type,
              std::vector<fuchsia::camera2::CameraStreamType> supported_streams)
      : type_(type),
        parent_node_(nullptr),
        output_buffer_collection_(std::move(output_buffer_collection)),
        output_image_formats_(output_image_formats),
        old_output_buffer_collection_(old_output_buffer_collection),
        isp_callback_{OnFrameAvailable, this},
        enabled_(false),
        supported_streams_(supported_streams) {
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
        supported_streams_(supported_streams) {
    ZX_ASSERT(type == NodeType::kGdc);
    configured_streams_.push_back(current_stream_type);
  }

  ~ProcessNode() {
    // We need to ensure that the child nodes
    // are destructed before parent node.
    child_nodes_info_.clear();

    // TODO(braval) : Remove this once we use buffercollectioninfo_2 where the buffer
    // collections will be part of camera processing nodes, and they will get destructed and
    // handles will be released. The ISP does not actually take ownership of the buffers upon
    // creating the stream (they are duplicated internally), so they must be manually released
    // here.
    if (type_ == NodeType::kInputStream) {
      ZX_ASSERT(ZX_OK == zx_handle_close_many(old_output_buffer_collection_.vmos,
                                              old_output_buffer_collection_.buffer_count));
    }
  }

  // Called when input is ready for this processing node.
  // For node type |kOutputStream| we will be just calling the registered
  // callbacks.
  void OnReadyToProcess(uint32_t buffer_index);

  // Callback function when frame is done processing for this node by the HW.
  // Here we would scan the |nodes_| and call each nodes |OnReadyToProcess()|
  void OnFrameAvailable(uint32_t buffer_index);
  void OnFrameAvailable(const frame_available_info_t* info);

  // Called by child nodes when the frame is released.
  // Here we would first free up the frame with the HW and then
  // call parents OnReleaseFrame()
  void OnReleaseFrame(uint32_t buffer_index);

  // Called by client
  void OnStartStreaming();
  void OnStopStreaming();

  // Helper APIs
  void set_isp_stream_protocol(std::unique_ptr<camera::IspStreamProtocol> isp_stream_protocol) {
    isp_stream_protocol_ = std::move(isp_stream_protocol);
  }
  void set_client_stream(std::unique_ptr<StreamImpl> client_stream) {
    client_stream_ = std::move(client_stream);
  }
  void set_task_index(uint32_t task_index) { hw_accelerator_task_index_ = task_index; }

  std::unique_ptr<camera::StreamImpl>& client_stream() { return client_stream_; }
  std::unique_ptr<camera::IspStreamProtocol>& isp_stream_protocol() { return isp_stream_protocol_; }
  NodeType type() { return type_; }

  // Returns this instance's callback parameter for use with the ISP Stream banjo interface.
  const output_stream_callback_t* isp_callback() { return &isp_callback_; }
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

  std::vector<fuchsia::camera2::CameraStreamType> configured_streams() {
    return configured_streams_;
  }

  std::vector<fuchsia::camera2::CameraStreamType> supported_streams() { return supported_streams_; }

  // Adds a child info in the vector
  void AddChildNodeInfo(ChildNodeInfo info) { child_nodes_info_.push_back(std::move(info)); }
  // Curent state of the node
  bool enabled() { return enabled_; }

 private:
  // Invoked by the ISP thread when a new frame is available.
  static void OnFrameAvailable(void* ctx, uint32_t buffer_id) {
    static_cast<ProcessNode*>(ctx)->OnFrameAvailable(buffer_id);
  }
  // Invoked by GDC or GE2D when a new frame is available.
  static void OnFrameAvailable(void* ctx, const frame_available_info_t* info) {
    static_cast<ProcessNode*>(ctx)->OnFrameAvailable(info);
  }
  // Invoked by GDC or GE2D on a Resolution change completion.
  // TODO: Implement this (Bug: 41730 @braval).
  static void OnResChange(void* ctx, const frame_available_info_t* info) {}

  bool AllChildNodesDisabled();
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
  // Temporary entry
  fuchsia_sysmem_BufferCollectionInfo old_output_buffer_collection_;
  // Valid for output node
  std::unique_ptr<StreamImpl> client_stream_;
  // Valid for input node
  std::unique_ptr<IspStreamProtocol> isp_stream_protocol_;
  // ISP callback
  output_stream_callback_t isp_callback_;
  // GDC/GE2D Frame callback
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
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PROCESSING_NODE_H_
