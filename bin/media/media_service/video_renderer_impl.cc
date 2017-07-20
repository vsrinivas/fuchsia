// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/media_service/video_renderer_impl.h"

#include "apps/media/lib/timeline/timeline.h"
#include "apps/tracing/lib/trace/event.h"
#include "lib/ftl/logging.h"

namespace media {

// static
std::shared_ptr<VideoRendererImpl> VideoRendererImpl::Create(
    fidl::InterfaceRequest<VideoRenderer> video_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<VideoRendererImpl>(
      new VideoRendererImpl(std::move(video_renderer_request),
                            std::move(media_renderer_request), owner));
}

VideoRendererImpl::VideoRendererImpl(
    fidl::InterfaceRequest<VideoRenderer> video_renderer_request,
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<VideoRenderer>(
          this,
          std::move(video_renderer_request),
          owner),
      video_frame_source_(std::make_shared<VideoFrameSource>()) {
  video_frame_source_->Bind(std::move(media_renderer_request));
}

VideoRendererImpl::~VideoRendererImpl() {}

void VideoRendererImpl::GetStatus(uint64_t version_last_seen,
                                  const GetStatusCallback& callback) {
  video_frame_source_->GetStatus(version_last_seen, callback);
}

void VideoRendererImpl::CreateView(
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FTL_DCHECK(video_frame_source_);
  new View(owner()->ConnectToEnvironmentService<mozart::ViewManager>(),
           std::move(view_owner_request), video_frame_source_);
}

VideoRendererImpl::View::View(
    mozart::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    std::shared_ptr<VideoFrameSource> video_frame_source)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Video Renderer"),
      video_frame_source_(video_frame_source),
      image_cycler_(session()) {
  parent_node().AddChild(image_cycler_);

  FTL_DCHECK(video_frame_source_);
  video_frame_source_->RegisterView(this);
}

VideoRendererImpl::View::~View() {
  video_frame_source_->UnregisterView(this);
}

void VideoRendererImpl::View::OnSceneInvalidated(
    mozart2::PresentationInfoPtr presentation_info) {
  TRACE_DURATION("motown", "OnSceneInvalidated");

  video_frame_source_->AdvanceReferenceTime(
      presentation_info->presentation_time);

  mozart::Size video_size = video_frame_source_->GetSize();
  if (!has_logical_size() || video_size.width == 0 || video_size.height == 0)
    return;

  // Update the image.
  const mozart::client::HostImage* image = image_cycler_.AcquireImage(
      video_size.width, video_size.height, video_size.width * 4u,
      mozart2::ImageInfo::PixelFormat::BGRA_8,
      mozart2::ImageInfo::ColorSpace::SRGB);
  FTL_DCHECK(image);
  video_frame_source_->GetRgbaFrame(static_cast<uint8_t*>(image->image_ptr()),
                                    video_size);
  image_cycler_.ReleaseAndSwapImage();

  // Scale the video so it fills the view.
  float width_scale = static_cast<float>(logical_size().width) /
                      static_cast<float>(video_size.width);
  float height_scale = static_cast<float>(logical_size().height) /
                       static_cast<float>(video_size.height);
  image_cycler_.SetScale(width_scale, height_scale, 1.f);
  image_cycler_.SetTranslation(logical_size().width * .5f,
                               logical_size().height * .5f, 0.f);

  if (video_frame_source_->views_should_animate()) {
    InvalidateScene();
  }
}

}  // namespace media
