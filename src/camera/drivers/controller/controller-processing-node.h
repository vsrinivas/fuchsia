// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_
#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include <ddktl/protocol/isp.h>

#include "configs/sherlock/internal-config.h"
#include "fbl/macros.h"
#include "isp_stream_protocol.h"
#include "memory_allocation.h"
#include "stream_protocol.h"
namespace camera {

class CameraProcessNode;
class StreamImpl;

// Output config details for HwAccelerator.
struct HwAcceleratorInfo {
  // |config_vmo| == Config VMO for GDC
  // |config_vmo| == Watermark for GE2D
  zx_handle_t config_vmo;
  std::vector<::fuchsia::sysmem::ImageFormat_2> image_formats;
};

struct ChildNodeInfo {
  // Pointer to the child node.
  std::shared_ptr<CameraProcessNode> child_node;
  // The Stream type/identifier for the child node.
  ::fuchsia::camera2::CameraStreamType stream_type;
  // The frame rate for this node.
  ::fuchsia::camera2::FrameRate output_frame_rate;
};

class CameraProcessNode {
 public:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(CameraProcessNode);
  CameraProcessNode(NodeType type, fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
                    fuchsia_sysmem_BufferCollectionInfo old_output_buffer_collection)
      : type_(type),
        output_buffer_collection_(std::move(output_buffer_collection)),
        old_output_buffer_collection_(old_output_buffer_collection),
        callback_{OnFrameAvailable, this},
        enabled_(false) {}

  explicit CameraProcessNode(NodeType type) : type_(type), enabled_(false) {}

  ~CameraProcessNode() {
    // TODO(braval) : Remove this once we use buffercollectioninfo_2 where the buffer collections
    // will be part of camera processing nodes, and they will get destructed and handles will be
    // released.
    // The ISP does not actually take ownership of the buffers upon creating the stream (they are
    // duplicated internally), so they must be manually released here.
    ZX_ASSERT(ZX_OK == zx_handle_close_many(old_output_buffer_collection_.vmos,
                                            old_output_buffer_collection_.buffer_count));
  }

  // Called when input is ready for this processing node.
  // For node type |kOutputStream| we will be just calling the registered
  // callbacks.
  void OnReadyToProcess(uint32_t buffer_index);
  // Callback function when frame is done processing for this node by the HW.
  // Here we would scan the |nodes_| and call each nodes |OnReadyToProcess()|
  void OnFrameAvailable(uint32_t buffer_index);
  // Called by child nodes when the frame is released.
  // Here we would first free up the frame with the HW and then
  // call parents OnReleaseFrame()
  void OnReleaseFrame(uint32_t buffer_index);
  // Called by client
  void OnStartStreaming();
  void OnStopStreaming();

  // Helper APIs
  void set_parent_node(const std::shared_ptr<CameraProcessNode> parent_node) {
    parent_node_ = parent_node;
  }
  void set_isp_stream_protocol(std::unique_ptr<camera::IspStreamProtocol> isp_stream_protocol) {
    isp_stream_protocol_ = std::move(isp_stream_protocol);
  }
  void set_client_stream(std::unique_ptr<StreamImpl> client_stream) {
    client_stream_ = std::move(client_stream);
  }

  std::unique_ptr<camera::StreamImpl>& client_stream() { return client_stream_; }
  std::unique_ptr<camera::IspStreamProtocol>& isp_stream_protocol() { return isp_stream_protocol_; }
  NodeType type() { return type_; }

  // Returns this instance's callback parameter for use with the ISP Stream banjo interface.
  const output_stream_callback_t* callback() { return &callback_; }

  // Adds a child info in the vector
  void AddChildNodeInfo(ChildNodeInfo info) { child_nodes_info_.push_back(std::move(info)); }
  // Curent state of the node
  bool enabled() { return enabled_; }

 private:
  // Invoked by the ISP thread when a new frame is available.
  static void OnFrameAvailable(void* ctx, uint32_t buffer_id) {
    static_cast<CameraProcessNode*>(ctx)->OnFrameAvailable(buffer_id);
  }
  bool AllChildNodesDisabled();
  // Type of node.
  NodeType type_;
  // List of all the children for this node.
  std::vector<ChildNodeInfo> child_nodes_info_;
  HwAcceleratorInfo hw_accelerator_;
  // Parent node
  std::shared_ptr<CameraProcessNode> parent_node_;
  // Input buffer collection is only valid for nodes other than
  // |kInputStream| and |kOutputStream|
  fuchsia::sysmem::BufferCollectionInfo_2 input_buffer_collection_;
  fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection_;
  // Temporary entry
  fuchsia_sysmem_BufferCollectionInfo old_output_buffer_collection_;
  // Valid for output node
  std::unique_ptr<StreamImpl> client_stream_;
  // Valid for input node
  std::unique_ptr<IspStreamProtocol> isp_stream_protocol_;
  // ISP callback
  output_stream_callback_t callback_;
  bool enabled_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_
