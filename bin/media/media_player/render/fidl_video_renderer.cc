// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/media_player/render/fidl_video_renderer.h"

#include <limits>

#include <trace/event.h>

#include "garnet/bin/media/media_player/fidl/fidl_type_conversions.h"
#include "lib/fxl/logging.h"

namespace media_player {

// static
std::shared_ptr<FidlVideoRenderer> FidlVideoRenderer::Create() {
  return std::make_shared<FidlVideoRenderer>();
}

FidlVideoRenderer::FidlVideoRenderer() {
  supported_stream_types_.push_back(VideoStreamTypeSet::Create(
      {StreamType::kVideoEncodingUncompressed},
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max()),
      Range<uint32_t>(0, std::numeric_limits<uint32_t>::max())));
}

FidlVideoRenderer::~FidlVideoRenderer() {}

void FidlVideoRenderer::Flush(bool hold_frame) {
  flushed_ = true;

  if (!packet_queue_.empty()) {
    if (hold_frame) {
      held_packet_ = std::move(packet_queue_.front());
    }

    while (!packet_queue_.empty()) {
      packet_queue_.pop();
    }
  }

  SetEndOfStreamPts(media::kUnspecifiedTime);

  InvalidateViews();
}

std::shared_ptr<PayloadAllocator> FidlVideoRenderer::allocator() {
  return nullptr;
}

Demand FidlVideoRenderer::SupplyPacket(PacketPtr packet) {
  FXL_DCHECK(packet);

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
  if (flushed_ || packet->payload() == nullptr || packet_pts_ns < min_pts(0)) {
    return need_more_packets() ? Demand::kPositive : Demand::kNegative;
  }

  if (!prime_callback_) {
    // We aren't priming, so put the packet on the queue regardless.
    packet_queue_.push(std::move(packet));

    // Discard old packets now in case our frame rate is so low that we have to
    // skip more packets than we demand when GetRgbaFrame is called.
    DiscardOldPackets();
  } else {
    // We're priming. Put the packet on the queue and determine whether we're
    // done priming.
    packet_queue_.push(std::move(packet));

    if (!need_more_packets()) {
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  // If this is the first packet to arrive, invalidate the views so the first
  // frame can be displayed.
  if (packet_queue_was_empty) {
    InvalidateViews();
  }

  return need_more_packets() ? Demand::kPositive : Demand::kNegative;
}

void FidlVideoRenderer::SetStreamType(const StreamType& stream_type) {
  converter_.SetStreamType(stream_type.Clone());
}

void FidlVideoRenderer::Prime(fxl::Closure callback) {
  flushed_ = false;

  if (packet_queue_.size() >= kPacketDemand || end_of_stream_pending()) {
    callback();
    return;
  }

  prime_callback_ = callback;
  stage()->SetDemand(Demand::kPositive);
}

geometry::Size FidlVideoRenderer::video_size() const {
  return converter_.GetSize();
}

geometry::Size FidlVideoRenderer::pixel_aspect_ratio() const {
  return converter_.GetPixelAspectRatio();
}

void FidlVideoRenderer::SetGeometryUpdateCallback(fxl::Closure callback) {
  geometry_update_callback_ = callback;
}

void FidlVideoRenderer::CreateView(
    fidl::InterfacePtr<views_v1::ViewManager> view_manager,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request) {
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

  if (need_more_packets()) {
    stage()->SetDemand(Demand::kPositive);
  }
}

void FidlVideoRenderer::GetRgbaFrame(uint8_t* rgba_buffer,
                                     const geometry::Size& rgba_buffer_size) {
  if (held_packet_) {
    converter_.ConvertFrame(rgba_buffer, rgba_buffer_size.width,
                            rgba_buffer_size.height, held_packet_->payload(),
                            held_packet_->size());
  } else if (!packet_queue_.empty()) {
    converter_.ConvertFrame(
        rgba_buffer, rgba_buffer_size.width, rgba_buffer_size.height,
        packet_queue_.front()->payload(), packet_queue_.front()->size());
  } else {
    memset(rgba_buffer, 0,
           rgba_buffer_size.width * rgba_buffer_size.height * 4);
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
    packet_queue_.pop();
    // Make sure the front of the queue has been checked for revised media
    // type.
    CheckForRevisedStreamType(packet_queue_.front());
  }
}

void FidlVideoRenderer::CheckForRevisedStreamType(const PacketPtr& packet) {
  FXL_DCHECK(packet);

  const std::unique_ptr<StreamType>& revised_stream_type =
      packet->revised_stream_type();

  if (revised_stream_type &&
      revised_stream_type->medium() == StreamType::Medium::kVideo &&
      revised_stream_type->video()) {
    converter_.SetStreamType(revised_stream_type->Clone());

    if (geometry_update_callback_) {
      geometry_update_callback_();
    }
  }
}

void FidlVideoRenderer::InvalidateViews() {
  for (auto& pair : views_) {
    pair.second->InvalidateScene();
  }
}

FidlVideoRenderer::View::View(
    views_v1::ViewManagerPtr view_manager,
    fidl::InterfaceRequest<views_v1_token::ViewOwner> view_owner_request,
    std::shared_ptr<FidlVideoRenderer> renderer)
    : mozart::BaseView(std::move(view_manager),
                       std::move(view_owner_request),
                       "Video Renderer"),
      renderer_(renderer),
      image_cycler_(session()) {
  FXL_DCHECK(renderer_);

  parent_node().AddChild(image_cycler_);
}

FidlVideoRenderer::View::~View() {}

void FidlVideoRenderer::View::OnSceneInvalidated(
    images::PresentationInfo presentation_info) {
  TRACE_DURATION("motown", "OnSceneInvalidated");

  renderer_->AdvanceReferenceTime(presentation_info.presentation_time);

  geometry::Size video_size = renderer_->video_size();
  if (!has_logical_size() || video_size.width == 0 || video_size.height == 0) {
    return;
  }

  // Update the image.
  const scenic_lib::HostImage* image = image_cycler_.AcquireImage(
      video_size.width, video_size.height, video_size.width * 4u,
      images::PixelFormat::BGRA_8, images::ColorSpace::SRGB);
  FXL_DCHECK(image);
  renderer_->GetRgbaFrame(static_cast<uint8_t*>(image->image_ptr()),
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

  if (renderer_->Progressing()) {
    InvalidateScene();
  }
}

}  // namespace media_player
