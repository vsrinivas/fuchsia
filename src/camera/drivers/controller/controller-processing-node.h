// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>

#include <vector>

#include <ddktl/protocol/gdc.h>
#include <ddktl/protocol/ge2d.h>

namespace camera {

class CameraProcessNode;

// Output config details for HwAccelerator.
struct HwAcceleratorInfo {
  // |config_vmo| == Config VMO for GDC
  // |config_vmo| == Watermark for GE2D
  zx_handle_t config_vmo;
  std::vector<::fuchsia::sysmem::ImageFormat_2> image_formats;
};

enum NodeType {
  kInputStream,
  kGdc,
  kGe2d,
  kOutputStream,
};

struct ChildNodeInfo {
  // Pointer to the child node.
  std::unique_ptr<CameraProcessNode> child_node;
  // The Stream type/identifier for the child node.
  ::fuchsia::camera2::CameraStreamType stream_type;
  // The frame rate for this node.
  ::fuchsia::camera2::FrameRate output_frame_rate;
};

class CameraProcessNode {
 public:
  static zx_status_t Create();

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

 private:
  // Type of node.
  NodeType type_;
  // List of all the children for this node.
  std::vector<ChildNodeInfo> child_nodes_info_;
  HwAcceleratorInfo hw_accelerator_;
  CameraProcessNode *parent_node_;
  // Input buffer collection is only valid for nodes other than
  // |kInputStream| and |kOutputStream|
  ::fuchsia::sysmem::BufferCollectionInfo_2 input_buffer_collection;
  ::fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection;
  bool enabled_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_CONTROLLER_PROCESSING_NODE_H_
