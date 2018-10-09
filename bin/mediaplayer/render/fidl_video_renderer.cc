// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/mediaplayer/render/fidl_video_renderer.h"

#include <trace/event.h>
#include <limits>
#include "garnet/bin/mediaplayer/fidl/fidl_type_conversions.h"
#include "garnet/bin/mediaplayer/graph/formatting.h"
#include "lib/fxl/logging.h"
#include "lib/media/timeline/timeline.h"

namespace media_player {

// static
std::shared_ptr<FidlVideoRenderer> FidlVideoRenderer::Create() {
  return std::make_shared<FidlVideoRenderer>();
}

FidlVideoRenderer::FidlVideoRenderer() : arrivals_(true), draws_(true) {
  supported_stream_types_.push_back(VideoStreamTypeSet::Create(
      {StreamType::kVideoEncodingUncompressed},
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max()),
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max())));
}

FidlVideoRenderer::~FidlVideoRenderer() {}

const char* FidlVideoRenderer::label() const { return "video_renderer"; }

void FidlVideoRenderer::Dump(std::ostream& os) const {
  Renderer::Dump(os);

  os << fostr::Indent;
  os << fostr::NewLine << "priming:               " << !!prime_callback_;
  os << fostr::NewLine << "flushed:               " << flushed_;
  os << fostr::NewLine << "presentation time:     " << AsNs(pts_ns_);
  os << fostr::NewLine << "video size:            " << video_size().width << "x"
     << video_size().height;
  os << fostr::NewLine
     << "pixel aspect ratio:    " << pixel_aspect_ratio().width << "x"
     << pixel_aspect_ratio().height;

  if (held_packet_) {
    os << fostr::NewLine << "held packet:           " << held_packet_;
  }

  if (!packet_queue_.empty()) {
    os << fostr::NewLine << "queued packets:" << fostr::Indent;

    for (auto& packet : packet_queue_) {
      os << fostr::NewLine << packet;
    }

    os << fostr::Outdent;
  }

  if (arrivals_.count() != 0) {
    os << fostr::NewLine << "video packet arrivals: " << fostr::Indent
       << arrivals_ << fostr::Outdent;
  }

  if (scenic_lead_.count() != 0) {
    os << fostr::NewLine << "packet availability on draw: " << fostr::Indent
       << draws_ << fostr::Outdent;
    os << fostr::NewLine << "scenic lead times:";
    os << fostr::NewLine << "    minimum           "
       << AsNs(scenic_lead_.min());
    os << fostr::NewLine << "    average           "
       << AsNs(scenic_lead_.average());
    os << fostr::NewLine << "    maximum           "
       << AsNs(scenic_lead_.max());
  }

  if (frame_rate_.progress_interval_count()) {
    os << fostr::NewLine << "scenic frame rate: " << fostr::Indent
       << frame_rate_ << fostr::Outdent;
  }

  os << fostr::Outdent;
}

void FidlVideoRenderer::ConfigureConnectors() {
  // TODO: Use ImagePipe and send the VMOs down the pipe.
  stage()->ConfigureInputToUseLocalMemory(0, kPacketDemand);
}

void FidlVideoRenderer::FlushInput(bool hold_frame, size_t input_index,
                                   fit::closure callback) {
  FXL_DCHECK(input_index == 0);
  FXL_DCHECK(callback);

  flushed_ = true;

  if (!packet_queue_.empty()) {
    if (hold_frame) {
      held_packet_ = std::move(packet_queue_.front());
    }

    while (!packet_queue_.empty()) {
      packet_queue_.pop_front();
    }
  }

  if (!hold_frame) {
    held_packet_.reset();
  }

  SetEndOfStreamPts(fuchsia::media::NO_TIMESTAMP);

  InvalidateViews();

  callback();
}

void FidlVideoRenderer::PutInputPacket(PacketPtr packet, size_t input_index) {
  FXL_DCHECK(packet);
  FXL_DCHECK(input_index == 0);

  int64_t packet_pts_ns = packet->GetPts(media::TimelineRate::NsPerSecond);

  if (packet->end_of_stream()) {
    SetEndOfStreamPts(packet_pts_ns);

    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  bool packet_queue_was_empty = packet_queue_.empty();
  if (packet_queue_was_empty) {
    // Make sure the front of the queue has been checked for revised media
    // type.
    CheckForRevisedStreamType(packet);
  }

  // Discard empty packets so they don't confuse the selection logic.
  // Discard packets that fall outside the program range.
  if (flushed_ || packet->payload() == nullptr || packet_pts_ns < min_pts(0) ||
      packet_pts_ns > max_pts(0)) {
    if (need_more_packets()) {
      stage()->RequestInputPacket();
    }

    return;
  }

  held_packet_.reset();

  packet_queue_.push_back(std::move(packet));

  int64_t now = media::Timeline::local_now();
  AdvanceReferenceTime(now);

  arrivals_.AddSample(now, current_timeline_function()(now), packet_pts_ns,
                      Progressing());

  // If this is the first packet to arrive, invalidate the views so the
  // first frame can be displayed.
  if (packet_queue_was_empty) {
    InvalidateViews();
  }

  if (need_more_packets()) {
    stage()->RequestInputPacket();
    return;
  }

  // We have enough packets. If we're priming, complete the operation.
  if (prime_callback_) {
    prime_callback_();
    prime_callback_ = nullptr;
  }
}

void FidlVideoRenderer::SetStreamType(const StreamType& stream_type) {
  FXL_DCHECK(stream_type.medium() == StreamType::Medium::kVideo);
  FXL_DCHECK(stream_type.encoding() == StreamType::kVideoEncodingUncompressed);

  const VideoStreamType& video_stream_type = *stream_type.video();

  // Assume we're not going to use the converter. This may change.
  use_converter_ = false;

  // TODO(dalesat): Fix |FfmpegVideoDecoder| plane layout before scenic YV12.
  // The fact that |VideoStreamType| has plane offsets is a artifact of the way
  // the decoder adds padding for ffmpeg decoders. The plan is to adjust
  // coded_height to make the planes contiguous. This has to happen before we
  // start using scenic's YV12 support, which isn't there yet. For now,
  // we convert from YV12 to ARGB in software, so we can accommodate the plane
  // offsets.
  scenic_line_stride_ = video_stream_type.line_stride().empty()
                            ? 0
                            : video_stream_type.line_stride()[0];

  switch (video_stream_type.pixel_format()) {
    case VideoStreamType::PixelFormat::kArgb:
      // Supported by scenic.
      scenic_pixel_format_ = fuchsia::images::PixelFormat::BGRA_8;
      break;
    case VideoStreamType::PixelFormat::kYuy2:
      // Supported by scenic.
      scenic_pixel_format_ = fuchsia::images::PixelFormat::YUY2;
      break;
    case VideoStreamType::PixelFormat::kNv12:
      // Supported by scenic.
      scenic_pixel_format_ = fuchsia::images::PixelFormat::NV12;
      break;
    case VideoStreamType::PixelFormat::kYv12:
      // Not supported by scenic, but we have a converter.
      converter_.SetStreamType(stream_type.Clone());
      use_converter_ = true;
      scenic_pixel_format_ = fuchsia::images::PixelFormat::BGRA_8;
      scenic_line_stride_ = video_stream_type.coded_width() * 4u;
      break;
    default:
      // Not supported.
      // TODO(dalesat): Report the problem.
      break;
  }

  stream_type_ = stream_type.Clone();
}

void FidlVideoRenderer::Prime(fit::closure callback) {
  flushed_ = false;

  if (packet_queue_.size() >= kPacketDemand || end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = std::move(callback);
  stage()->RequestInputPacket();
}

fuchsia::math::Size FidlVideoRenderer::video_size() const {
  fuchsia::math::Size size;

  if (stream_type_) {
    FXL_DCHECK(stream_type_->medium() == StreamType::Medium::kVideo);
    const VideoStreamType& video_stream_type = *stream_type_->video();
    size.width = video_stream_type.width();
    size.height = video_stream_type.height();
  } else {
    size.width = 0;
    size.height = 0;
  }

  return size;
}

fuchsia::math::Size FidlVideoRenderer::pixel_aspect_ratio() const {
  fuchsia::math::Size pixel_aspect_ratio;

  if (stream_type_) {
    FXL_DCHECK(stream_type_->medium() == StreamType::Medium::kVideo);
    const VideoStreamType& video_stream_type = *stream_type_->video();
    pixel_aspect_ratio.width = video_stream_type.pixel_aspect_ratio_width();
    pixel_aspect_ratio.height = video_stream_type.pixel_aspect_ratio_height();
  } else {
    pixel_aspect_ratio.width = 1;
    pixel_aspect_ratio.height = 1;
  }

  return pixel_aspect_ratio;
}

void FidlVideoRenderer::SetGeometryUpdateCallback(fit::closure callback) {
  geometry_update_callback_ = std::move(callback);
}

void FidlVideoRenderer::CreateView(
    fidl::InterfacePtr<::fuchsia::ui::viewsv1::ViewManager> view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request) {
  auto view =
      std::make_unique<View>(std::move(view_manager),
                             std::move(view_owner_request), shared_from_this());
  View* view_raw_ptr = view.get();
  views_.emplace(view_raw_ptr, std::move(view));

  view_raw_ptr->SetReleaseHandler(
      [this, view_raw_ptr]() { views_.erase(view_raw_ptr); });
}

void FidlVideoRenderer::AdvanceReferenceTime(int64_t reference_time) {
  UpdateTimeline(reference_time);

  pts_ns_ = current_timeline_function()(reference_time);

  DiscardOldPackets();
}

void FidlVideoRenderer::GetFrame(uint8_t* buffer,
                                 const fuchsia::math::Size& buffer_size) {
  if (!held_packet_ && packet_queue_.empty()) {
    // No packet. Show black.
    FillBlack(buffer, buffer_size);
    return;
  }

  PacketPtr packet = held_packet_ ? held_packet_ : packet_queue_.front();

  if (use_converter_) {
    converter_.ConvertFrame(buffer, buffer_size.width, buffer_size.height,
                            packet->payload(), packet->size());
  } else {
    // TODO(dalesat): This copy goes away when we use ImagePipe.
    memcpy(buffer, packet->payload(), packet->size());
  }
}

void FidlVideoRenderer::OnProgressStarted() {
  held_packet_.reset();
  InvalidateViews();
}

void FidlVideoRenderer::DiscardOldPackets() {
  // We keep at least one packet around even if it's old, so we can show an
  // old frame rather than no frame when we starve.
  while (packet_queue_.size() > 1 &&
         packet_queue_.front()->GetPts(media::TimelineRate::NsPerSecond) <
             pts_ns_) {
    // TODO(dalesat): Add hysteresis.
    packet_queue_.pop_front();
    // Make sure the front of the queue has been checked for revised media
    // type.
    CheckForRevisedStreamType(packet_queue_.front());
  }
}

void FidlVideoRenderer::CheckForRevisedStreamType(const PacketPtr& packet) {
  FXL_DCHECK(packet);

  const std::unique_ptr<StreamType>& revised_stream_type =
      packet->revised_stream_type();

  if (revised_stream_type) {
    if (revised_stream_type->medium() == StreamType::Medium::kVideo) {
      FXL_DCHECK(revised_stream_type->video());

      SetStreamType(*revised_stream_type);

      if (geometry_update_callback_) {
        geometry_update_callback_();
      }
    } else {
      FXL_LOG(FATAL) << "Revised stream type was not video.";
    }
  }
}

void FidlVideoRenderer::InvalidateViews() {
  for (auto& pair : views_) {
    pair.second->InvalidateScene();
  }
}

void FidlVideoRenderer::OnSceneInvalidated(int64_t reference_time) {
  AdvanceReferenceTime(reference_time);

  // Update trackers.
  int64_t now = media::Timeline::local_now();
  draws_.AddSample(
      now, current_timeline_function()(now),
      packet_queue_.empty()
          ? Packet::kUnknownPts
          : packet_queue_.front()->GetPts(media::TimelineRate::NsPerSecond),
      Progressing());
  scenic_lead_.AddSample(reference_time - now);
  frame_rate_.AddSample(now, Progressing());

  if (need_more_packets()) {
    stage()->RequestInputPacket();
  }
}

void FidlVideoRenderer::FillBlack(uint8_t* buffer,
                                  const fuchsia::math::Size& buffer_size) {
  FXL_DCHECK(buffer);

  uint32_t plane_stride = scenic_line_stride_ * buffer_size.height;

  // TODO(dalesat): Make sure these are correct.

  switch (scenic_pixel_format_) {
    case fuchsia::images::PixelFormat::BGRA_8:
    case fuchsia::images::PixelFormat::YUY2:
      // Interleaved, so just line stride times number of lines. In both cases,
      // zeros map to black.
      memset(buffer, 0, plane_stride);
      break;
    case fuchsia::images::PixelFormat::NV12:
      // Two planes:
      // 1) Y: one byte per pixel, so line stride times number of lines. Y must
      //    be zero for black.
      // 2) Interleaved UV: two bytes (U and V) for every 2x2 pixels, so half
      //    the size of the first plane. U and V must be 128 for black.
      memset(buffer, 0, plane_stride);
      memset(buffer + plane_stride, 128, plane_stride / 2);
      break;
    case fuchsia::images::PixelFormat::YV12:
      memset(buffer, 0, plane_stride);
      memset(buffer + plane_stride, 128, plane_stride / 2);
      break;
  }
}

////////////////////////////////////////////////////////////////////////////////
// FidlVideoRenderer::View implementation.

FidlVideoRenderer::View::View(
    ::fuchsia::ui::viewsv1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<::fuchsia::ui::viewsv1token::ViewOwner>
        view_owner_request,
    std::shared_ptr<FidlVideoRenderer> renderer)
    : mozart::BaseView(std::move(view_manager), std::move(view_owner_request),
                       "Video Renderer"),
      renderer_(renderer),
      image_cycler_(session()) {
  FXL_DCHECK(renderer_);

  parent_node().AddChild(image_cycler_);
}

FidlVideoRenderer::View::~View() {}

void FidlVideoRenderer::View::OnSceneInvalidated(
    fuchsia::images::PresentationInfo presentation_info) {
  TRACE_DURATION("motown", "OnSceneInvalidated");

  renderer_->OnSceneInvalidated(presentation_info.presentation_time);

  fuchsia::math::Size video_size = renderer_->video_size();
  if (!has_logical_size() || video_size.width == 0 || video_size.height == 0) {
    return;
  }

  // Update the image.
  const scenic::HostImage* image = image_cycler_.AcquireImage(
      video_size.width, video_size.height, renderer_->scenic_line_stride(),
      renderer_->scenic_pixel_format(), fuchsia::images::ColorSpace::SRGB);
  FXL_DCHECK(image);

  // There's no way to find out how big the buffer is, so we have to assume
  // |HostImageCycler| got it right.
  renderer_->GetFrame(static_cast<uint8_t*>(image->image_ptr()), video_size);
  image_cycler_.ReleaseAndSwapImage();

  // Scale the video so it fills the view.
  float width_scale = static_cast<float>(logical_size().width) /
                      static_cast<float>(video_size.width);
  float height_scale = static_cast<float>(logical_size().height) /
                       static_cast<float>(video_size.height);
  image_cycler_.SetScale(width_scale, height_scale, 1.f);
  image_cycler_.SetTranslation(logical_size().width * .5f,
                               logical_size().height * .5f, 0.f);

  if (renderer_->Progressing()) {
    InvalidateScene();
  }
}

}  // namespace media_player
