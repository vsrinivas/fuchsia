// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_DRIVERS_CONTROLLER_PASSTHROUGH_NODE_H_
#define SRC_CAMERA_DRIVERS_CONTROLLER_PASSTHROUGH_NODE_H_

#include <fuchsia/camera2/cpp/fidl.h>
#include <fuchsia/hardware/ge2d/cpp/banjo.h>

#include <utility>

#include "src/camera/drivers/controller/memory_allocation.h"
#include "src/camera/drivers/controller/processing_node.h"
#include "src/camera/drivers/controller/stream_pipeline_info.h"
#include "src/camera/drivers/controller/util.h"

// |PassthroughNode| performs no operations aside from dropping frames. It is used to synchronize
// the phase of multiple lower-framerate consumers.
namespace camera {

class PassthroughNode : public ProcessNode {
 public:
  PassthroughNode(async_dispatcher_t* dispatcher, BufferAttachments attachments,
                  FrameCallback frame_callback,
                  const camera::InternalConfigNode& internal_passthrough_node);
  static fpromise::result<std::unique_ptr<PassthroughNode>, zx_status_t> Create(
      async_dispatcher_t* dispatcher, BufferAttachments attachments, FrameCallback frame_callback,
      const InternalConfigNode& internal_passthrough_node, const StreamCreationData& info);

 private:
  // ProcessNode method implementations.
  void ProcessFrame(FrameToken token, frame_metadata_t metadata) override;
  void SetOutputFormat(uint32_t output_format_index, fit::closure callback) override;
  void ShutdownImpl(fit::closure callback) override;

  // fuchsia.hardware.camerahwaccel.*Callback implementations.
  void HwFrameReady(frame_available_info_t info) override;
  void HwFrameResolutionChanged(frame_available_info_t info) override;
  void HwTaskRemoved(task_remove_status_t status) override;
};

}  // namespace camera

#endif  // SRC_CAMERA_DRIVERS_CONTROLLER_PASSTHROUGH_NODE_H_
