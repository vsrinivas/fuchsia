// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>
#include <memory>
#include <vector>

#include "apps/media/services/media_renderer.fidl.h"
#include "apps/media/services/video_renderer.fidl.h"
#include "apps/media/src/media_service/media_service_impl.h"
#include "apps/media/src/video/video_frame_source.h"
#include "apps/mozart/lib/view_framework/base_view.h"
#include "apps/mozart/services/buffers/cpp/buffer_producer.h"
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
  void GetVideoSize(const GetVideoSizeCallback& callback) override;

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
    void OnDraw() override;

    // Creates a node for the video.
    mozart::NodePtr MakeVideoNode(mozart::TransformPtr transform,
                                  const mozart::SceneUpdatePtr& update);

    // Draws the video texture image and returns its resource.
    mozart::ResourcePtr DrawVideoTexture(const mozart::Size& size,
                                         int64_t presentation_time);

    mozart::BufferProducer buffer_producer_;
    std::shared_ptr<VideoFrameSource> video_frame_source_;
    TimelineFunction timeline_function_;

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
