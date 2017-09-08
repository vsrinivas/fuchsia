// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "lib/media/fidl/media_renderer.fidl.h"
#include "lib/media/fidl/video_renderer.fidl.h"
#include "garnet/bin/media/media_service/media_service_impl.h"
#include "garnet/bin/media/video/video_frame_source.h"
#include "lib/ui/scenic/client/host_image_cycler.h"
#include "lib/ui/view_framework/base_view.h"
#include "lib/fidl/cpp/bindings/binding.h"

namespace media {

// Fidl agent that renders video.
class VideoRendererImpl : public MediaServiceImpl::Product<VideoRenderer>,
                          public VideoRenderer {
 public:
  static std::shared_ptr<VideoRendererImpl> Create(
      fidl::InterfaceRequest<VideoRenderer> video_renderer_request,
      fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
      MediaServiceImpl* owner);

  ~VideoRendererImpl() override;

  // VideoRenderer implementation.
  void GetStatus(uint64_t version_last_seen,
                 const GetStatusCallback& callback) override;

  void CreateView(
      fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) override;

 private:
  class View : public mozart::BaseView {
   public:
    View(mozart::ViewManagerPtr view_manager,
         fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
         std::shared_ptr<VideoFrameSource> video_frame_source);

    ~View() override;

   private:
    // |BaseView|:
    void OnSceneInvalidated(
        scenic::PresentationInfoPtr presentation_info) override;

    std::shared_ptr<VideoFrameSource> video_frame_source_;
    TimelineFunction timeline_function_;

    scenic_lib::HostImageCycler image_cycler_;

    FTL_DISALLOW_COPY_AND_ASSIGN(View);
  };

  VideoRendererImpl(
      fidl::InterfaceRequest<VideoRenderer> video_renderer_request,
      fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
      MediaServiceImpl* owner);

  std::shared_ptr<VideoFrameSource> video_frame_source_;

  FTL_DISALLOW_COPY_AND_ASSIGN(VideoRendererImpl);
};

}  // namespace media
