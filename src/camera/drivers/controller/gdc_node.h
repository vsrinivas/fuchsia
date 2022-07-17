// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/gdc/cpp/banjo.h>

#include "src/camera/drivers/controller/configs/product_config.h"
#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/drivers/controller/util.h"

// |GdcNode| represents a |ProcessNode| which would talk to the
// GDC driver.
namespace camera {

class GdcNode : public ProcessNode {
 public:
  GdcNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
          FrameCallback frame_callback, const ddk::GdcProtocolClient& gdc);

  // Creates a |GdcNode| object.
  // Args:
  // |memory_allocator| : Memory allocator object to allocate memory using sysmem.
  // |dispatcher| : Dispatcher on which GDC tasks can be queued up.
  // |device| : Device pointer to get the GDC configs from DDK.
  // |gdc| : GDC protocol to talk to the driver.
  // |info| : StreamCreationData for the requested stream.
  // |parent_node| : pointer to the node to which we need to append this |OutputNode|.
  // |internal_output_node| : InternalConfigNode corresponding to this node.
  static fpromise::result<std::unique_ptr<ProcessNode>, zx_status_t> Create(
      async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
      const LoadFirmwareCallback& load_firmware, const ddk::GdcProtocolClient& gdc,
      const InternalConfigNode& internal_gdc_node, const StreamCreationData& info);

 private:
  // ProcessNode method implementations.
  void ProcessFrame(FrameToken token, frame_metadata_t metadata) override;
  void SetOutputFormat(uint32_t output_format_index, fit::closure callback) override;
  void ShutdownImpl(fit::closure callback) override;

  // fuchsia.hardware.camerahwaccel.*Callback implementations.
  void HwFrameReady(frame_available_info_t info) override;
  void HwFrameResolutionChanged(frame_available_info_t info) override;
  void HwTaskRemoved(task_remove_status_t status) override;

  // Protocol to talk to the GDC driver.
  ddk::GdcProtocolClient gdc_;
  // Task index for this node.
  uint32_t task_index_;
  std::queue<FrameToken> input_frame_queue_;
  fit::closure shutdown_callback_;
  fit::closure format_callback_;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_GDC_NODE_H_
