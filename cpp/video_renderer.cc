// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/cpp/video_renderer.h"

#include <limits>

#include "apps/media/cpp/timeline.h"
#include "apps/media/cpp/timeline_function.h"

namespace media {

VideoRenderer::VideoRenderer()
    : renderer_binding_(this),
      control_point_binding_(this),
      timeline_consumer_binding_(this) {
  // Make sure the PTS rate for all packets is nanoseconds.
  SetPtsRate(TimelineRate::NsPerSecond);
}

VideoRenderer::~VideoRenderer() {}

void VideoRenderer::Bind(
    fidl::InterfaceRequest<MediaRenderer> renderer_request) {
  renderer_binding_.Bind(std::move(renderer_request));
}

mozart::Size VideoRenderer::GetSize() {
  return converter_.GetSize();
}

void VideoRenderer::GetRgbaFrame(uint8_t* rgba_buffer,
                                 const mozart::Size& rgba_buffer_size,
                                 int64_t reference_time) {
  MaybeApplyPendingTimelineChange(reference_time);
  MaybePublishEndOfStream();

  pts_ = current_timeline_function_(reference_time);

  DiscardOldPackets();

  // TODO(dalesat): Detect starvation.

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

void VideoRenderer::GetSupportedMediaTypes(
    const GetSupportedMediaTypesCallback& callback) {
  VideoMediaTypeSetDetailsPtr video_details = VideoMediaTypeSetDetails::New();
  video_details->min_width = 1;
  video_details->max_width = std::numeric_limits<uint32_t>::max();
  video_details->min_height = 1;
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
  callback(std::move(supported_types));
}

void VideoRenderer::SetMediaType(MediaTypePtr media_type) {
  FTL_DCHECK(media_type);
  FTL_DCHECK(media_type->details);
  const VideoMediaTypeDetailsPtr& details = media_type->details->get_video();
  FTL_DCHECK(details);

  converter_.SetMediaType(media_type);
}

void VideoRenderer::GetPacketConsumer(
    fidl::InterfaceRequest<MediaPacketConsumer> packet_consumer_request) {
  MediaPacketConsumerBase::Bind(std::move(packet_consumer_request));
}

void VideoRenderer::GetTimelineControlPoint(
    fidl::InterfaceRequest<MediaTimelineControlPoint> control_point_request) {
  control_point_binding_.Bind(std::move(control_point_request));
}

void VideoRenderer::OnPacketSupplied(
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

  packet_queue_.push(std::move(supplied_packet));

  // Discard old packets now in case our frame rate is so low that we have to
  // skip more packets than we demand when GetRgbaFrame is called.
  DiscardOldPackets();
}

void VideoRenderer::OnFlushRequested(const FlushCallback& callback) {
  while (!packet_queue_.empty()) {
    packet_queue_.pop();
  }
  MaybeClearEndOfStream();
  callback();
}

void VideoRenderer::OnFailure() {
  // TODO(dalesat): Report this to our owner.
  if (renderer_binding_.is_bound()) {
    renderer_binding_.Close();
  }

  if (control_point_binding_.is_bound()) {
    control_point_binding_.Close();
  }

  if (timeline_consumer_binding_.is_bound()) {
    timeline_consumer_binding_.Close();
  }

  MediaPacketConsumerBase::OnFailure();
}

void VideoRenderer::GetStatus(uint64_t version_last_seen,
                              const GetStatusCallback& callback) {
  if (version_last_seen < status_version_) {
    CompleteGetStatus(callback);
  } else {
    pending_status_callbacks_.push_back(callback);
  }
}

void VideoRenderer::GetTimelineConsumer(
    fidl::InterfaceRequest<TimelineConsumer> timeline_consumer_request) {
  timeline_consumer_binding_.Bind(std::move(timeline_consumer_request));
}

void VideoRenderer::Prime(const PrimeCallback& callback) {
  pts_ = kUnspecifiedTime;
  SetDemand(2);
  callback();  // TODO(dalesat): Wait until we get packets.
}

void VideoRenderer::SetTimelineTransform(
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
}

void VideoRenderer::DiscardOldPackets() {
  // We keep at least one packet around even if it's old, so we can show an
  // old frame rather than no frame when we starve.
  while (packet_queue_.size() > 1 &&
         packet_queue_.front()->packet()->pts < pts_) {
    // TODO(dalesat): Add hysteresis.
    packet_queue_.pop();
  }
}

void VideoRenderer::ClearPendingTimelineFunction(bool completed) {
  pending_timeline_function_ =
      TimelineFunction(kUnspecifiedTime, kUnspecifiedTime, 1, 0);
  if (set_timeline_transform_callback_) {
    set_timeline_transform_callback_(completed);
    set_timeline_transform_callback_ = nullptr;
  }
}

void VideoRenderer::MaybeApplyPendingTimelineChange(int64_t reference_time) {
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

void VideoRenderer::MaybeClearEndOfStream() {
  if (end_of_stream_pts_ != kUnspecifiedTime) {
    end_of_stream_pts_ = kUnspecifiedTime;
    end_of_stream_published_ = false;
    SendStatusUpdates();
  }
}

void VideoRenderer::MaybePublishEndOfStream() {
  if (!end_of_stream_published_ && end_of_stream_pts_ != kUnspecifiedTime &&
      current_timeline_function_(Timeline::local_now()) >= end_of_stream_pts_) {
    end_of_stream_published_ = true;
    SendStatusUpdates();
  }
}

void VideoRenderer::SendStatusUpdates() {
  ++status_version_;

  std::vector<GetStatusCallback> pending_status_callbacks;
  pending_status_callbacks_.swap(pending_status_callbacks);

  for (const GetStatusCallback& pending_status_callback :
       pending_status_callbacks) {
    CompleteGetStatus(pending_status_callback);
  }
}

void VideoRenderer::CompleteGetStatus(const GetStatusCallback& callback) {
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
