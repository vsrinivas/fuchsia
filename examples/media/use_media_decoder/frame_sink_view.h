// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_FRAME_SINK_VIEW_H_
#define GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_FRAME_SINK_VIEW_H_

#include <fuchsia/mediacodec/cpp/fidl.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fxl/macros.h>
#include <lib/ui/scenic/cpp/resources.h>
#include <lib/ui/view_framework/base_view.h>

// Sets up an ImagePipe (including scene graph aspects) such that FrameSink can
// push frames to all the ImagePipe(s) of all the FrameSinkView(s) that are
// currently active.
//
// Registers with parent on construction and de-registers on destruction.  Only
// called on the thread that's running |loop|.
class FrameSink;
class FrameSinkView : public mozart::BaseView {
 public:
  static std::unique_ptr<FrameSinkView> Create(
      FrameSink* parent, async::Loop* main_loop,
      ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
      fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
          view_owner_request);

  ~FrameSinkView() override;

  // This is very similar to FrameSink's PutFrame, which fans out to all the
  // alive FrameSinkView(s).  This method is the leaf of that fan-out.
  void PutFrame(
      uint32_t image_id, zx_time_t present_time, const zx::vmo& vmo,
      uint64_t vmo_offset,
      const fuchsia::mediacodec::VideoUncompressedFormat& video_format,
      fit::closure on_done);

 private:
  FrameSinkView(FrameSink* parent, async::Loop* loop,
                ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
                fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
                    view_owner_request);

  // From mozart::BaseView. Called when the scene is "invalidated".
  void OnSceneInvalidated(
      fuchsia::images::PresentationInfo presentation_info) override;

  FrameSink* parent_;

  async::Loop* main_loop_;

  scenic::ShapeNode node_;

  fidl::InterfacePtr<fuchsia::images::ImagePipe> image_pipe_;

  FXL_DISALLOW_COPY_AND_ASSIGN(FrameSinkView);
};

#endif  // GARNET_EXAMPLES_MEDIA_USE_MEDIA_DECODER_FRAME_SINK_VIEW_H_
