// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_service/video_renderer_impl.h"

#include <trace/event.h>

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media {

// static
std::shared_ptr<VideoRendererImpl> VideoRendererImpl::Create(
    f1dl::InterfaceRequest<VideoRenderer> video_renderer_request,
    f1dl::InterfaceRequest<MediaRenderer> media_renderer_request,
    MediaServiceImpl* owner) {
  return std::shared_ptr<VideoRendererImpl>(
      new VideoRendererImpl(std::move(video_renderer_request),
                            std::move(media_renderer_request), owner));
}

VideoRendererImpl::VideoRendererImpl(
    f1dl::InterfaceRequest<VideoRenderer> video_renderer_request,
    f1dl::InterfaceRequest<MediaRenderer> media_renderer_request,
    MediaServiceImpl* owner)
    : MediaServiceImpl::Product<VideoRenderer>(
          this,
          std::move(video_renderer_request),
          owner),
      media_renderer_binding_(this, std::move(media_renderer_request)),
      video_frame_source_(std::make_shared<VideoFrameSource>()) {
  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(media_renderer_binding_)));

  video_frame_source_->SetStreamTypeRevisedCallback(
      [this]() { status_publisher_.SendUpdates(); });

  status_publisher_.SetCallbackRunner(
      [this](const GetStatusCallback& callback, uint64_t version) {
        VideoRendererStatusPtr status = VideoRendererStatus::New();
        status->video_size = video_frame_source_->converter().GetSize().Clone();
        status->pixel_aspect_ratio =
            video_frame_source_->converter().GetPixelAspectRatio().Clone();
        callback(version, std::move(status));
      });
}

VideoRendererImpl::~VideoRendererImpl() {
  video_frame_source_->RemoveAllViews();
}

void VideoRendererImpl::GetSupportedMediaTypes(
    const GetSupportedMediaTypesCallback& callback) {
  callback(SupportedMediaTypes());
}

void VideoRendererImpl::SetMediaType(MediaTypePtr media_type) {
  if (!media_type || !media_type->details || !media_type->details->is_video()) {
    FXL_LOG(ERROR) << "Invalid argument to SetMediaType call.";
    if (media_renderer_binding_.is_bound()) {
      media_renderer_binding_.Unbind();
    }

    return;
  }

  const VideoMediaTypeDetailsPtr& details = media_type->details->get_video();
  FXL_DCHECK(details);

  video_frame_source_->converter().SetStreamType(
      media_type.To<std::unique_ptr<StreamType>>());
  status_publisher_.SendUpdates();

  FLOG(log_channel_, SetMediaType(std::move(media_type)));
}

void VideoRendererImpl::GetPacketConsumer(
    f1dl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request) {
  video_frame_source_->BindConsumer(std::move(packet_consumer_request));
}

void VideoRendererImpl::GetTimelineControlPoint(
    f1dl::InterfaceRequest<MediaTimelineControlPoint> control_point_request) {
  video_frame_source_->BindTimelineControlPoint(
      std::move(control_point_request));
}

void VideoRendererImpl::GetStatus(uint64_t version_last_seen,
                                  const GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void VideoRendererImpl::CreateView(
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request) {
  FXL_DCHECK(video_frame_source_);
  new View(owner()->ConnectToEnvironmentService<mozart::ViewManager>(),
           std::move(view_owner_request), video_frame_source_);
}

f1dl::Array<MediaTypeSetPtr> VideoRendererImpl::SupportedMediaTypes() {
  VideoMediaTypeSetDetailsPtr video_details = VideoMediaTypeSetDetails::New();
  video_details->min_width = 0;
  video_details->max_width = std::numeric_limits<uint32_t>::max();
  video_details->min_height = 0;
  video_details->max_height = std::numeric_limits<uint32_t>::max();
  MediaTypeSetPtr supported_type = MediaTypeSet::New();
  supported_type->medium = MediaTypeMedium::VIDEO;
  supported_type->details = MediaTypeSetDetails::New();
  supported_type->details->set_video(std::move(video_details));
  supported_type->encodings = f1dl::Array<f1dl::String>::New(1);
  supported_type->encodings[0] = MediaType::kVideoEncodingUncompressed;
  f1dl::Array<MediaTypeSetPtr> supported_types =
      f1dl::Array<MediaTypeSetPtr>::New(1);
  supported_types[0] = std::move(supported_type);
  return supported_types;
}

VideoRendererImpl::View::View(
    mozart::ViewManagerPtr view_manager,
    f1dl::InterfaceRequest<mozart::ViewOwner> view_owner_request,
    std::shared_ptr<VideoFrameSource> video_frame_source)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Video Renderer"),
      video_frame_source_(video_frame_source),
      image_cycler_(session()) {
  FXL_DCHECK(video_frame_source_);
  video_frame_source_->AddView(this);

  parent_node().AddChild(image_cycler_);

  SetReleaseHandler([this]() { video_frame_source_->RemoveView(this); });
}

VideoRendererImpl::View::~View() {}

void VideoRendererImpl::View::OnSceneInvalidated(
    ui_mozart::PresentationInfoPtr presentation_info) {
  TRACE_DURATION("motown", "OnSceneInvalidated");

  video_frame_source_->AdvanceReferenceTime(
      presentation_info->presentation_time);

  mozart::Size video_size = video_frame_source_->converter().GetSize();
  if (!has_logical_size() || video_size.width == 0 || video_size.height == 0)
    return;

  // Update the image.
  const scenic_lib::HostImage* image = image_cycler_.AcquireImage(
      video_size.width, video_size.height, video_size.width * 4u,
      scenic::ImageInfo::PixelFormat::BGRA_8,
      scenic::ImageInfo::ColorSpace::SRGB);
  FXL_DCHECK(image);
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
