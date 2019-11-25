// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CAMERA_SIMPLE_CAMERA_SIMPLE_CAMERA_LIB_VIDEO_DISPLAY_H_
#define SRC_CAMERA_SIMPLE_CAMERA_SIMPLE_CAMERA_LIB_VIDEO_DISPLAY_H_

#include <fuchsia/camera/cpp/fidl.h>
#include <fuchsia/images/cpp/fidl.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/eventpair.h>

#include <deque>
#include <list>

#include "src/camera/simple_camera/simple_camera_lib/buffer_fence.h"
#include "src/camera/simple_camera/simple_camera_lib/frame_scheduler.h"
#include "src/lib/fxl/macros.h"

namespace simple_camera {

using OnShutdownCallback = fit::function<void()>;
using OnFrameAvailableCallback = fit::function<zx_status_t(fuchsia::camera::FrameAvailableEvent)>;

class VideoDisplay {
 public:
  // Connect to a camera with |camera_id|. If the camera exists, and can be
  // connected to, configures the camera to the first available format, and
  // starts streaming data over the image pipe.
  // Returns an error if the initial part of setup fails.  If ZX_OK is
  // returned, termination of communication is signalled by calling |callback|,
  // which may be done on an arbitrary thread.
  zx_status_t ConnectToCamera(sys::ComponentContext* context, uint32_t camera_id,
                              fidl::InterfaceHandle<fuchsia::images::ImagePipe> image_pipe,
                              OnShutdownCallback callback);

  void DisconnectFromCamera();

  VideoDisplay() = default;

 private:
  // Called when the driver tells us a new frame is available:
  zx_status_t IncomingBufferFilled(const fuchsia::camera::FrameAvailableEvent& frame);

  // Called when a buffer is released by the consumer.
  void BufferReleased(uint32_t buffer_id);

  zx_status_t SetupBuffers(const fuchsia::sysmem::BufferCollectionInfo& buffer_collection);

  // The number of buffers to allocate while setting up the camera stream.
  // This number has to be at least 2, since scenic will hold onto one buffer
  // at all times.
  static constexpr uint16_t kNumberOfBuffers = 16;

  // Image pipe to send to display
  fuchsia::images::ImagePipePtr image_pipe_;

  // Callback to that we're closing communication:
  OnShutdownCallback on_shut_down_callback_ = nullptr;

  std::vector<std::unique_ptr<BufferFence>> frame_buffers_;

  SimpleFrameScheduler frame_scheduler_;

  class CameraClient {
   public:
    fuchsia::camera::ManagerSyncPtr manager_;
    fuchsia::camera::StreamPtr stream_;
  };
  std::unique_ptr<CameraClient> camera_client_;
  zx::eventpair stream_token_;

  FXL_DISALLOW_COPY_AND_ASSIGN(VideoDisplay);
};

}  // namespace simple_camera

#endif  // SRC_CAMERA_SIMPLE_CAMERA_SIMPLE_CAMERA_LIB_VIDEO_DISPLAY_H_
