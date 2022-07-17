// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>

#include <utility>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/drivers/controller/util.h"

// |Ge2dNode| represents a |ProcessNode| which would talk to the
// GE2D driver.
namespace camera {

class Ge2dNode : public ProcessNode {
 public:
  Ge2dNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
           FrameCallback frame_callback, const ddk::Ge2dProtocolClient& ge2d,
           const camera::InternalConfigNode& internal_ge2d_node);
  // Creates a |Ge2dNode| object.
  // Args:
  // |memory_allocator| : Memory allocator object to allocate memory using sysmem.
  // |dispatcher| : Dispatcher on which GE2D tasks can be queued up.
  // |device| : Device pointer to get the GE2D configs from DDK.
  // |ge2d|   : GE2D protocol to talk to the driver.
  // |info|   : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  static fpromise::result<std::unique_ptr<Ge2dNode>, zx_status_t> Create(
      async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
      const LoadFirmwareCallback& load_firmware, const ddk::Ge2dProtocolClient& ge2d,
      const InternalConfigNode& internal_ge2d_node, const StreamCreationData& info);

  // Special functionality provided by the GE2D node.
  // Notifies that the client has requested a new crop rectangle.
  // (x_min, y_min): Top left co-ordinates for the crop rectangle.
  // (x_max, y_max): Bottom right coordinates for the crop rectangle.
  // These are in the range [0.0f, 1.0f]
  zx_status_t SetCropRect(float x_min, float y_min, float x_max, float y_max);

 private:
  // ProcessNode method implementations.
  void ProcessFrame(FrameToken token, frame_metadata_t metadata) override;
  void SetOutputFormat(uint32_t output_format_index, fit::closure callback) override;
  void ShutdownImpl(fit::closure callback) override;

  // fuchsia.hardware.camerahwaccel.*Callback implementations.
  void HwFrameReady(frame_available_info_t info) override;
  void HwFrameResolutionChanged(frame_available_info_t info) override;
  void HwTaskRemoved(task_remove_status_t status) override;

  // Protocol to talk to the GE2D driver.
  ddk::Ge2dProtocolClient ge2d_;
  // Task index for this node.
  uint32_t task_index_;
  // Task type.
  Ge2DConfig task_type_;
  bool in_place_ = false;
  // Stores the transform last requested to GE2D.
  resize_info_t current_transform_;
  std::queue<FrameToken> input_frame_queue_;
  fit::closure shutdown_callback_;
  fit::closure format_callback_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_
