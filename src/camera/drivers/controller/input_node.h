// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_INPUT_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_INPUT_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/isp/cpp/banjo.h>
#include <lib/trace/event.h>
#include <zircon/assert.h>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"

// |InputNode| represents a |ProcessNode| which would talk to the
// ISP driver.
namespace camera {

class InputNode : public ProcessNode {
 public:
  InputNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
            FrameCallback frame_callback, const ddk::IspProtocolClient& isp);

  // Creates an |InputNode| object.
  // 1. Creates the ISP stream protocol
  // 2. Creates the requested ISP stream
  // 3. Allocates buffers if needed
  // Args:
  // |info| : StreamCreationData for the requested stream.
  // |memory_allocator| : Memory allocator object to allocate memory using sysmem.
  // |dispatcher| : Dispatcher on which GDC tasks can be queued up.
  // |isp| : ISP protocol to talk to the driver.
  static fpromise::result<std::unique_ptr<InputNode>, zx_status_t> Create(
      async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
      const ddk::IspProtocolClient& isp, const StreamCreationData& info);

  // Special functionality provided by the ISP node.
  void StartStreaming();
  void StopStreaming();

 private:
  // ProcessNode method implementations.
  void ProcessFrame(FrameToken token, frame_metadata_t metadata) override;
  void SetOutputFormat(uint32_t output_format_index, fit::closure callback) override;
  void ShutdownImpl(fit::closure callback) override;

  // fuchsia.hardware.camerahwaccel.*Callback implementations.
  void HwFrameReady(frame_available_info_t info) override;
  void HwFrameResolutionChanged(frame_available_info_t info) override;
  void HwTaskRemoved(task_remove_status_t status) override;

  // Protocol to talk to ISP driver.
  ddk::IspProtocolClient isp_;
  // Protocol to talk to ISP stream.
  ddk::OutputStreamProtocolClient stream_;
  fit::closure shutdown_callback_;
  zx::time last_frame_error_logged_ = zx::time::infinite_past();
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_INPUT_NODE_H_
