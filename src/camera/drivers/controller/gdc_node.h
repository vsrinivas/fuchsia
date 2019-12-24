// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/camera2/hal/cpp/fidl.h>
#include <zircon/assert.h>

#include <ddktl/protocol/gdc.h>

#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/lib/format_conversion/buffer_collection_helper.h"
#include "src/camera/lib/format_conversion/format_conversion.h"

// |GdcNode| represents a |ProcessNode| which would talk to the
// GDC driver.
namespace camera {

fit::result<gdc_config_info, zx_status_t> LoadGdcConfiguration(
    zx_device_t* device, const camera::GdcConfig& config_type);

// Invoked by GDC driver when a new frame is available.
static void OnGdcFrameAvailable(void* ctx, const frame_available_info_t* info) {
  static_cast<ProcessNode*>(ctx)->OnFrameAvailable(info);
}

// Invoked by GDC on a Resolution change completion.
// TODO(41730): Implement this.
static void OnGdcResChange(void* ctx, const frame_available_info_t* info) {}

// Invoked by GDC when a new frame is available.
// TODO(braval): Implement OnTaskRemoved() to handle shutdown.
static void OnGdcTaskRemoved(void* ctx, task_remove_status_t status) {}

class GdcNode : public ProcessNode {
 public:
  GdcNode(async_dispatcher_t* dispatcher, const ddk::GdcProtocolClient& gdc,
          ProcessNode* parent_node,
          std::vector<fuchsia::sysmem::ImageFormat_2> output_image_formats,
          fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
          fuchsia::camera2::CameraStreamType current_stream_type,
          std::vector<fuchsia::camera2::CameraStreamType> supported_streams)
      : ProcessNode(gdc, NodeType::kGdc, parent_node, output_image_formats,
                    std::move(output_buffer_collection), current_stream_type, supported_streams),
        dispatcher_(dispatcher),
        gdc_(gdc),
        frame_callback_{OnGdcFrameAvailable, this},
        res_callback_{OnGdcResChange, this},
        remove_task_callback_{OnGdcTaskRemoved, this} {}

  ~GdcNode() { OnShutdown(); }

  // Creates a |GdcNode| object.
  // Args:
  // |memory_allocator| : Memory allocator object to allocate memory using sysmem.
  // |dispatcher| : Dispatcher on which GDC tasks can be queued up.
  // |device| : Device pointer to get the GDC configs from DDK.
  // |gdc| : GDC protocol to talk to the driver.
  // |info| : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  static fit::result<ProcessNode*, zx_status_t> CreateGdcNode(
      ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
      zx_device_t* device, const ddk::GdcProtocolClient& gdc, StreamCreationData* info,
      ProcessNode* parent_node, const InternalConfigNode& internal_gdc_node);

  void set_task_index(uint32_t task_index) { task_index_ = task_index; }

  const hw_accel_frame_callback_t* frame_callback() { return &frame_callback_; }
  const hw_accel_res_change_callback_t* res_callback() { return &res_callback_; }

  const hw_accel_remove_task_callback_t* remove_task_callback() { return &remove_task_callback_; }

  // Notifies that a frame is ready to be sent to the client.
  void OnReadyToProcess(uint32_t buffer_index) override;

  // Releases the frame |buffer_index| back to the GDC driver.
  void OnReleaseFrame(uint32_t buffer_index) override;

  // Removes the registered task with the GDC driver.
  void OnShutdown() override;

 private:
  __UNUSED async_dispatcher_t* dispatcher_;
  // Protocol to talk to the GDC driver.
  ddk::GdcProtocolClient gdc_;
  // Task index for this node.
  uint32_t task_index_;
  hw_accel_frame_callback_t frame_callback_;
  hw_accel_res_change_callback_t res_callback_;
  hw_accel_remove_task_callback_t remove_task_callback_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_
