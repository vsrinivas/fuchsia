// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>

#include <ddktl/protocol/gdc.h>

#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"

// |GdcNode| represents a |ProcessNode| which would talk to the
// GDC driver.
namespace camera {

fit::result<gdc_config_info, zx_status_t> LoadGdcConfiguration(
    zx_device_t* device, ProductConfig& product_config, const camera::GdcConfig& config_type);

// Invoked by GDC driver when a new frame is available.
void OnGdcFrameAvailable(void* ctx, const frame_available_info_t* info);

// Invoked by GDC on a Resolution change completion.
void OnGdcResChange(void* ctx, const frame_available_info_t* info);

class GdcNode : public ProcessNode {
 public:
  GdcNode(async_dispatcher_t* dispatcher, const ddk::GdcProtocolClient& gdc,
          ProcessNode* parent_node, const camera::InternalConfigNode& internal_gdc_node,
          fuchsia::sysmem::BufferCollectionInfo_2 output_buffer_collection,
          fuchsia::camera2::CameraStreamType current_stream_type,
          uint32_t current_image_format_index)
      : ProcessNode(NodeType::kGdc, parent_node, current_stream_type,
                    internal_gdc_node.image_formats, std::move(output_buffer_collection),
                    internal_gdc_node.supported_streams, dispatcher,
                    internal_gdc_node.output_frame_rate, current_image_format_index),
        gdc_(gdc),
        frame_callback_{OnGdcFrameAvailable, this},
        res_callback_{OnGdcResChange, this},
        remove_task_callback_{OnGdcTaskRemoved, this} {}

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
      const ControllerMemoryAllocator& memory_allocator, async_dispatcher_t* dispatcher,
      zx_device_t* device, const ddk::GdcProtocolClient& gdc, StreamCreationData* info,
      ProcessNode* parent_node, const InternalConfigNode& internal_gdc_node);

  void set_task_index(uint32_t task_index) { task_index_ = task_index; }

  const hw_accel_frame_callback_t* frame_callback() { return &frame_callback_; }
  const hw_accel_res_change_callback_t* res_callback() { return &res_callback_; }

  const hw_accel_remove_task_callback_t* remove_task_callback() { return &remove_task_callback_; }

  // Notifies that a frame is done processing by this node.
  void OnFrameAvailable(const frame_available_info_t* info) override;

  // Notifies that a frame is ready to be sent to the client.
  void OnReadyToProcess(const frame_available_info_t* info) override;

  // Releases the frame |buffer_index| back to the GDC driver.
  void OnReleaseFrame(uint32_t buffer_index) override;

  // Removes the registered task with the GDC driver.
  void OnShutdown(fit::function<void(void)> shutdown_callback) override;

  // Marks the GDC shutdown callback received.
  void OnTaskRemoved(zx_status_t status);

  // Notifies that the client has requested to change resolution.
  void OnResolutionChangeRequest(uint32_t output_format_index) override;

 private:
  // Invoked by GDC when a new frame is available.
  static void OnGdcTaskRemoved(void* ctx, task_remove_status_t status) {
    static_cast<GdcNode*>(ctx)->OnTaskRemoved(status);
  }

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
