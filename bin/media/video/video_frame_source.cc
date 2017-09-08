// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/video/video_frame_source.h"

#include <limits>

#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

VideoFrameSource::VideoFrameSource() : media_renderer_binding_(this) {
  // Make sure the PTS rate for all packets is nanoseconds.
  SetPtsRate(TimelineRate::NsPerSecond);

  // We accept revised media types.
  AcceptRevisedMediaType();

  timeline_control_point_.SetProgramRangeSetCallback(
      [this](uint64_t program, int64_t min_pts, int64_t max_pts) {
        FTL_DCHECK(program == 0) << "Non-zero program not implemented";
        min_pts_ = min_pts;
      });

  timeline_control_point_.SetPrimeRequestedCallback(
      [this](const MediaTimelineControlPoint::PrimeCallback& callback) {
        SetDemand(kPacketDemand);

        if (packet_queue_.size() >= kPacketDemand) {
          callback();
        } else {
          prime_callback_ = callback;
        }
      });

  timeline_control_point_.SetProgressStartedCallback([this]() {
    held_packet_.reset();
    InvalidateViews();
  });

  status_publisher_.SetCallbackRunner(
      [this](const VideoRenderer::GetStatusCallback& callback,
             uint64_t version) {
        VideoRendererStatusPtr status = VideoRendererStatus::New();
        status->video_size = converter_.GetSize().Clone();
        status->pixel_aspect_ratio = converter_.GetPixelAspectRatio().Clone();
        callback(version, std::move(status));
      });
}

VideoFrameSource::~VideoFrameSource() {
  // Close the bindings before members are destroyed so we don't try to
  // destroy any callbacks that are pending on open channels.

  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.Close();
  }

  timeline_control_point_.Reset();
}

void VideoFrameSource::Bind(
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  media_renderer_binding_.Bind(std::move(media_renderer_request));
  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(media_renderer_binding_)));
  FLOG(log_channel_,
       Config(SupportedMediaTypes(),
              FLOG_ADDRESS(static_cast<MediaPacketConsumerBase*>(this)),
              FLOG_ADDRESS(&timeline_control_point_)));
}

void VideoFrameSource::AdvanceReferenceTime(int64_t reference_time) {
  uint32_t generation;
  timeline_control_point_.SnapshotCurrentFunction(
      reference_time, &current_timeline_function_, &generation);

  pts_ = current_timeline_function_(reference_time);

  DiscardOldPackets();

  if (packet_queue_.empty()) {
    FLOG(log_channel_, EngagePacket(pts_, MediaPacket::kNoTimestamp, 0));
  } else {
    FLOG(log_channel_, EngagePacket(pts_, packet_queue_.front()->packet()->pts,
                                    packet_queue_.front()->label()));
  }
}

void VideoFrameSource::GetRgbaFrame(uint8_t* rgba_buffer,
                                    const mozart::Size& rgba_buffer_size) {
  if (held_packet_) {
    converter_.ConvertFrame(rgba_buffer, rgba_buffer_size.width,
                            rgba_buffer_size.height, held_packet_->payload(),
                            held_packet_->payload_size());
  } else if (!packet_queue_.empty()) {
    converter_.ConvertFrame(rgba_buffer, rgba_buffer_size.width,
                            rgba_buffer_size.height,
                            packet_queue_.front()->payload(),
                            packet_queue_.front()->payload_size());
  } else {
    memset(rgba_buffer, 0,
           rgba_buffer_size.width * rgba_buffer_size.height * 4);
  }
}

void VideoFrameSource::GetStatus(
    uint64_t version_last_seen,
    const VideoRenderer::GetStatusCallback& callback) {
  status_publisher_.Get(version_last_seen, callback);
}

void VideoFrameSource::GetSupportedMediaTypes(
    const GetSupportedMediaTypesCallback& callback) {
  callback(SupportedMediaTypes());
}

void VideoFrameSource::SetMediaType(MediaTypePtr media_type) {
  // TODO(dalesat): Shouldn't DCHECK these...need an RCHECK.
  FTL_DCHECK(media_type);
  FTL_DCHECK(media_type->details);
  const VideoMediaTypeDetailsPtr& details = media_type->details->get_video();
  FTL_DCHECK(details);

  converter_.SetMediaType(media_type);
  status_publisher_.SendUpdates();

  FLOG(log_channel_, SetMediaType(std::move(media_type)));
}

void VideoFrameSource::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request) {
  if (is_bound()) {
    Reset();
  }

  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void VideoFrameSource::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> control_point_request) {
  timeline_control_point_.Bind(std::move(control_point_request));
}

fidl::Array<MediaTypeSetPtr> VideoFrameSource::SupportedMediaTypes() {
  VideoMediaTypeSetDetailsPtr video_details = VideoMediaTypeSetDetails::New();
  video_details->min_width = 0;
  video_details->max_width = std::numeric_limits<uint32_t>::max();
  video_details->min_height = 0;
  video_details->max_height = std::numeric_limits<uint32_t>::max();
  MediaTypeSetPtr supported_type = MediaTypeSet::New();
  supported_type->medium = MediaTypeMedium::VIDEO;
  supported_type->details = MediaTypeSetDetails::New();
  supported_type->details->set_video(std::move(video_details));
  supported_type->encodings = fidl::Array<fidl::String>::New(1);
  supported_type->encodings[0] = MediaType::kVideoEncodingUncompressed;
  fidl::Array<MediaTypeSetPtr> supported_types =
      fidl::Array<MediaTypeSetPtr>::New(1);
  supported_types[0] = std::move(supported_type);
  return supported_types;
}

void VideoFrameSource::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FTL_DCHECK(supplied_packet);
  FTL_DCHECK(supplied_packet->packet()->pts_rate_ticks ==
             TimelineRate::NsPerSecond.subject_delta());
  FTL_DCHECK(supplied_packet->packet()->pts_rate_seconds ==
             TimelineRate::NsPerSecond.reference_delta());

  if (supplied_packet->packet()->end_of_stream) {
    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }

    timeline_control_point_.SetEndOfStreamPts(supplied_packet->packet()->pts);
  }

  // Discard empty packets so they don't confuse the selection logic.
  if (supplied_packet->payload() == nullptr) {
    return;
  }

  bool packet_queue_was_empty = packet_queue_.empty();
  if (packet_queue_was_empty) {
    // Make sure the front of the queue has been checked for revised media
    // type.
    CheckForRevisedMediaType(supplied_packet->packet());
  }

  if (supplied_packet->packet()->pts < min_pts_) {
    // This packet falls outside the program range. Discard it.
    return;
  }

  if (!prime_callback_) {
    // We aren't priming, so put the packet on the queue regardless.
    packet_queue_.push(std::move(supplied_packet));

    // Discard old packets now in case our frame rate is so low that we have to
    // skip more packets than we demand when GetRgbaFrame is called.
    DiscardOldPackets();
  } else {
    // We're priming. Put the packet on the queue and determine whether we're
    // done priming.
    packet_queue_.push(std::move(supplied_packet));

    if (packet_queue_.size() + (held_packet_ ? 1 : 0) >= kPacketDemand) {
      prime_callback_();
      prime_callback_ = nullptr;
    }
  }

  // If this is the first packet to arrive and we're not telling the views to
  // animate, invalidate the views so the first frame can be displayed.
  if (packet_queue_was_empty && !views_should_animate()) {
    InvalidateViews();
  }
}

void VideoFrameSource::OnFlushRequested(bool hold_frame,
                                        const FlushCallback& callback) {
  if (!packet_queue_.empty()) {
    if (hold_frame) {
      held_packet_ = std::move(packet_queue_.front());
    }

    while (!packet_queue_.empty()) {
      packet_queue_.pop();
    }
  }

  timeline_control_point_.ClearEndOfStream();
  callback();
  InvalidateViews();
}

void VideoFrameSource::OnFailure() {
  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.Close();
  }

  timeline_control_point_.Reset();

  MediaPacketConsumerBase::OnFailure();
}

void VideoFrameSource::DiscardOldPackets() {
  // We keep at least one packet around even if it's old, so we can show an
  // old frame rather than no frame when we starve.
  while (packet_queue_.size() > 1 &&
         packet_queue_.front()->packet()->pts < pts_) {
    // TODO(dalesat): Add hysteresis.
    packet_queue_.pop();
    // Make sure the front of the queue has been checked for revised media
    // type.
    CheckForRevisedMediaType(packet_queue_.front()->packet());
  }
}

void VideoFrameSource::CheckForRevisedMediaType(const MediaPacketPtr& packet) {
  FTL_DCHECK(packet);

  const MediaTypePtr& revised_media_type = packet->revised_media_type;

  if (revised_media_type && revised_media_type->details &&
      revised_media_type->details->get_video()) {
    converter_.SetMediaType(revised_media_type);
    status_publisher_.SendUpdates();
  }
}

}  // namespace media
