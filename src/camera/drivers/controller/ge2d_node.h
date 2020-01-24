// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <zircon/assert.h>

#include <ddktl/protocol/ge2d.h>

#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"

// |Ge2dNode| represents a |ProcessNode| which would talk to the
// GE2D driver.
namespace camera {

// Invoked by GE2D driver when a new frame is available.
void OnGe2dFrameAvailable(void* ctx, const frame_available_info_t* info);

// Invoked by GE2D on a Resolution change completion.
void OnGe2dResChange(void* ctx, const frame_available_info_t* info);

// Invoked by GE2D when task is removed.
void OnGe2dTaskRemoved(void* ctx, task_remove_status_t status);

class Ge2dNode : public ProcessNode {
 public:
  Ge2dNode(async_dispatcher_t* dispatcher, const ddk::Ge2dProtocolClient& ge2d,
           ProcessNode* parent_node,
           std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
           fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
           fuchsia::camera2::CameraStreamType current_stream_type,
           std::vector<fuchsia::camera2::CameraStreamType> supported_streams,
           fuchsia::camera2::FrameRate frame_rate, const resize_info_t& info)
      : ProcessNode(NodeType::kGe2d, parent_node, output_image_formats,
                    std::move(output_buffer_collection), current_stream_type, supported_streams,
                    dispatcher, frame_rate),
        ge2d_(ge2d),
        info_(info),
        frame_callback_{OnGe2dFrameAvailable, this},
        res_callback_{OnGe2dResChange, this},
        remove_task_callback_{OnGe2dTaskRemoved, this} {}

  // Creates a |Ge2dNode| object.
  // Args:
  // |memory_allocator| : Memory allocator object to allocate memory using sysmem.
  // |dispatcher| : Dispatcher on which GE2D tasks can be queued up.
  // |device| : Device pointer to get the GE2D configs from DDK.
  // |ge2d|   : GE2D protocol to talk to the driver.
  // |info|   : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  static fit::result<ProcessNode*, zx_status_t> CreateGe2dNode(
      ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
      zx_device_t* device, const ddk::Ge2dProtocolClient& ge2d, StreamCreationData* info,
      ProcessNode* parent_node, const InternalConfigNode& internal_ge2d_node);

  void set_task_index(uint32_t task_index) { task_index_ = task_index; }

  const resize_info_t* resize_info() { return &info_; }
  const hw_accel_frame_callback_t* frame_callback() { return &frame_callback_; }
  const hw_accel_res_change_callback_t* res_callback() { return &res_callback_; }
  const hw_accel_remove_task_callback_t* remove_task_callback() { return &remove_task_callback_; }

  // Notifies that a frame is done processing by this node.
  virtual void OnFrameAvailable(const frame_available_info_t* info) override;

  // Notifies that a frame is ready to be sent to the client.
  void OnReadyToProcess(const frame_available_info_t* info) override;

  // Releases the frame |buffer_index| back to the GE2D driver.
  void OnReleaseFrame(uint32_t buffer_index) override;

  // Removes the registered task with the GE2D driver.
  void OnShutdown(fit::function<void(void)> shutdown_callback) override;

  // Marks the GE2D shutdown callback received.
  void OnTaskRemoved(zx_status_t status);

 private:
  // Protocol to talk to the GE2D driver.
  ddk::Ge2dProtocolClient ge2d_;
  // Task index for this node.
  uint32_t task_index_;
  resize_info_t info_;
  hw_accel_frame_callback_t frame_callback_;
  hw_accel_res_change_callback_t res_callback_;
  hw_accel_remove_task_callback_t remove_task_callback_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_GE2D_NODE_H_
