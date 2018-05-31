// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <deque>
#include <list>

#include <lib/app/cpp/application_context.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/fxl/macros.h>
#include <lib/ui/scenic/client/resources.h>
#include <lib/ui/view_framework/base_view.h>
#include <fbl/vector.h>

#include <garnet/examples/ui/video_display/camera_client.h>
#include <garnet/examples/ui/video_display/fake_camera_source.h>
#include <garnet/examples/ui/video_display/fenced_buffer.h>
#include <garnet/examples/ui/video_display/frame_scheduler.h>


namespace video_display {

class View : public mozart::BaseView {
 public:
  View(async::Loop* loop, component::ApplicationContext* application_context,
       ::fuchsia::ui::views_v1::ViewManagerPtr view_manager,
       fidl::InterfaceRequest<::fuchsia::ui::views_v1_token::ViewOwner> view_owner_request,
       bool use_fake_camera);

  ~View() override;

 private:
  // Called when the driver tells us a new frame is available:
  zx_status_t IncomingBufferFilled(const camera_vb_frame_notify_t& frame);

  // Called to reserve a buffer for writing.  Currently, this is only called
  // by IncomingBufferFilled.  It should be possible to get notified that the
  // frame is being written, and get a pipelining benefit from notifying
  // scenic earlier.  Scenic would have to allow erroneous frames to be
  // cancelled though.
  zx_status_t ReserveIncomingBuffer(FencedBuffer* buffer,
                                    uint64_t capture_time_ns);

  // Called when a buffer is released by the consumer.
  void BufferReleased(FencedBuffer* buffer);

  // Callbacks from asyncronous interface:
  zx_status_t OnGetFormats(
      const std::vector<camera_video_format_t>& out_formats);
  zx_status_t OnSetFormat(uint64_t max_frame_size);

  // From mozart::BaseView. Called on a mouse or keyboard event.
  virtual bool OnInputEvent(fuchsia::ui::input::InputEvent event) override;

  // From mozart::BaseView. Called when the scene is "invalidated".
  // Invalidation should happen when the surfaces change, but not
  // necessarily when a texture changes.
  void OnSceneInvalidated(fuchsia::images::PresentationInfo presentation_info) override;

  // Creates a new buffer and registers an image with scenic.  If the buffer
  // already exists, returns a pointer to that buffer.  Buffer is not required
  // to be valid. If it is nullptr, the returned status can be used to check if
  // that buffer is now available.
  // TODO(garratt): There is currently no way to detect overlapping or unused
  // frames to remove them.
  zx_status_t FindOrCreateBuffer(uint32_t frame_size,
                                 uint64_t vmo_offset,
                                 FencedBuffer** buffer,
                                 const camera_video_format_t& format);

  // The currently selected format.
  camera_video_format_t format_;

  async::Loop* const loop_;
  // The number of buffers to allocate while setting up the camera stream.
  // This number has to be at least 2, since scenic will hold onto one buffer
  // at all times.
  static constexpr uint16_t kNumberOfBuffers = 8;
  scenic_lib::ShapeNode node_;

  // Image pipe to send to display
  fuchsia::images::ImagePipePtr image_pipe_;

  std::vector<std::unique_ptr<FencedBuffer>> frame_buffers_;
  uint32_t last_buffer_index_ = 0;
  uint64_t max_frame_size_ = 0;

  zx::vmo vmo_;
  SimpleFrameScheduler frame_scheduler_;
  std::unique_ptr<CameraInterfaceBase> video_source_;

  FXL_DISALLOW_COPY_AND_ASSIGN(View);
};

}  // namespace video_display
