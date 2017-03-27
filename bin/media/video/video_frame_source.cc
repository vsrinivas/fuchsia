// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/src/video/video_frame_source.h"

#include <limits>

#include "apps/media/lib/timeline/timeline.h"
#include "apps/media/lib/timeline/timeline_function.h"

namespace media {

VideoFrameSource::VideoFrameSource()
    : media_renderer_binding_(this),
      control_point_binding_(this),
      timeline_consumer_binding_(this) {
  // Make sure the PTS rate for all packets is nanoseconds.
  SetPtsRate(TimelineRate::NsPerSecond);

  // We accept revised media types.
  AcceptRevisedMediaType();

  status_publisher_.SetCallbackRunner(
      [this](const VideoRenderer::GetStatusCallback& callback,
             uint64_t version) {
        VideoRendererStatusPtr status = VideoRendererStatus::New();
        status->video_size = converter_.GetSize().Clone();
        callback(version, std::move(status));
      });
}

VideoFrameSource::~VideoFrameSource() {
  // Close the bindings before members are destroyed so we don't try to
  // destroy any callbacks that are pending on open channels.

  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.Close();
  }

  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  if (timeline_consumer_binding_.is_bound()) {
    timeline_consumer_binding_.Close();
  }
}

void VideoFrameSource::Bind(
    fidl::InterfaceRequest<MediaRenderer> media_renderer_request) {
  media_renderer_binding_.Bind(std::move(media_renderer_request));
  FLOG(log_channel_, BoundAs(FLOG_BINDING_KOID(media_renderer_binding_)));
  FLOG(log_channel_,
       Config(SupportedMediaTypes(),
              FLOG_ADDRESS(static_cast<MediaPacketConsumerBase*>(this))));
}

void VideoFrameSource::AdvanceReferenceTime(int64_t reference_time) {
  MaybeApplyPendingTimelineChange(reference_time);
  MaybePublishEndOfStream();

  pts_ = current_timeline_function_(reference_time);

  DiscardOldPackets();

  // TODO(dalesat): Detect starvation.
}

void VideoFrameSource::GetRgbaFrame(uint8_t* rgba_buffer,
                                    const mozart::Size& rgba_buffer_size) {
  if (packet_queue_.empty()) {
    memset(rgba_buffer, 0,
           rgba_buffer_size.width * rgba_buffer_size.height * 4);
  } else {
    converter_.ConvertFrame(rgba_buffer, rgba_buffer_size.width,
                            rgba_buffer_size.height,
                            packet_queue_.front()->payload(),
                            packet_queue_.front()->payload_size());
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
  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void VideoFrameSource::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> control_point_request) {
  control_point_binding_.Bind(std::move(control_point_request));
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
    end_of_stream_pts_ = supplied_packet->packet()->pts;
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

  packet_queue_.push(std::move(supplied_packet));

  // Discard old packets now in case our frame rate is so low that we have to
  // skip more packets than we demand when GetRgbaFrame is called.
  DiscardOldPackets();

  // If this is the first packet to arrive and we're not telling the views to
  // animate, invalidate the views so the first frame can be displayed.
  if (packet_queue_was_empty && !views_should_animate()) {
    InvalidateViews();
  }
}

void VideoFrameSource::OnFlushRequested(const FlushCallback& callback) {
  while (!packet_queue_.empty()) {
    packet_queue_.pop();
  }
  MaybeClearEndOfStream();
  callback();
  InvalidateViews();
}

void VideoFrameSource::OnFailure() {
  if (media_renderer_binding_.is_bound()) {
    media_renderer_binding_.Close();
  }

  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  if (timeline_consumer_binding_.is_bound()) {
    timeline_consumer_binding_.Close();
  }

  MediaPacketConsumerBase::OnFailure();
}

void VideoFrameSource::GetStatus(uint64_t version_last_seen,
                                 const GetStatusCallback& callback) {
  if (version_last_seen < status_version_) {
    CompleteGetStatus(callback);
  } else {
    pending_status_callbacks_.push_back(callback);
  }
}

void VideoFrameSource::GetTimelineConsumer(
    fidl::InterfaceRequest<TimelineConsumer> timeline_consumer_request) {
  timeline_consumer_binding_.Bind(std::move(timeline_consumer_request));
}

void VideoFrameSource::Prime(const PrimeCallback& callback) {
  pts_ = kUnspecifiedTime;
  SetDemand(2);
  callback();  // TODO(dalesat): Wait until we get packets.
}

void VideoFrameSource::SetTimelineTransform(
    TimelineTransformPtr timeline_transform,
    const SetTimelineTransformCallback& callback) {
  FTL_DCHECK(timeline_transform);
  FTL_DCHECK(timeline_transform->reference_delta != 0);

  if (timeline_transform->subject_time != kUnspecifiedTime) {
    MaybeClearEndOfStream();
  }

  int64_t reference_time =
      timeline_transform->reference_time == kUnspecifiedTime
          ? Timeline::local_now()
          : timeline_transform->reference_time;
  int64_t subject_time = timeline_transform->subject_time == kUnspecifiedTime
                             ? current_timeline_function_(reference_time)
                             : timeline_transform->subject_time;

  // Eject any previous pending change.
  ClearPendingTimelineFunction(false);

  // Queue up the new pending change.
  pending_timeline_function_ = TimelineFunction(
      reference_time, subject_time, timeline_transform->reference_delta,
      timeline_transform->subject_delta);

  set_timeline_transform_callback_ = callback;

  InvalidateViews();
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

void VideoFrameSource::ClearPendingTimelineFunction(bool completed) {
  pending_timeline_function_ =
      TimelineFunction(kUnspecifiedTime, kUnspecifiedTime, 1, 0);
  if (set_timeline_transform_callback_) {
    set_timeline_transform_callback_(completed);
    set_timeline_transform_callback_ = nullptr;
  }
}

void VideoFrameSource::MaybeApplyPendingTimelineChange(int64_t reference_time) {
  if (pending_timeline_function_.reference_time() == kUnspecifiedTime ||
      pending_timeline_function_.reference_time() > reference_time) {
    return;
  }

  current_timeline_function_ = pending_timeline_function_;
  pending_timeline_function_ =
      TimelineFunction(kUnspecifiedTime, kUnspecifiedTime, 1, 0);

  if (set_timeline_transform_callback_) {
    set_timeline_transform_callback_(true);
    set_timeline_transform_callback_ = nullptr;
  }

  SendStatusUpdates();
}

void VideoFrameSource::MaybeClearEndOfStream() {
  if (end_of_stream_pts_ != kUnspecifiedTime) {
    end_of_stream_pts_ = kUnspecifiedTime;
    end_of_stream_published_ = false;
    SendStatusUpdates();
  }
}

void VideoFrameSource::MaybePublishEndOfStream() {
  if (!end_of_stream_published_ && end_of_stream_pts_ != kUnspecifiedTime &&
      current_timeline_function_(Timeline::local_now()) >= end_of_stream_pts_) {
    end_of_stream_published_ = true;
    SendStatusUpdates();
  }
}

void VideoFrameSource::SendStatusUpdates() {
  ++status_version_;

  std::vector<GetStatusCallback> pending_status_callbacks;
  pending_status_callbacks_.swap(pending_status_callbacks);

  for (const GetStatusCallback& pending_status_callback :
       pending_status_callbacks) {
    CompleteGetStatus(pending_status_callback);
  }
}

void VideoFrameSource::CompleteGetStatus(const GetStatusCallback& callback) {
  MediaTimelineControlPointStatusPtr status =
      MediaTimelineControlPointStatus::New();
  status->timeline_transform =
      TimelineTransform::From(current_timeline_function_);
  status->end_of_stream =
      end_of_stream_pts_ != kUnspecifiedTime &&
      current_timeline_function_(Timeline::local_now()) >= end_of_stream_pts_;
  callback(status_version_, std::move(status));
}

}  // namespace media
