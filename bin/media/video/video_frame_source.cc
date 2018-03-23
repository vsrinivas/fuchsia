// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/media/video/video_frame_source.h"

#include <limits>

#include "garnet/bin/media/fidl/fidl_type_conversions.h"
#include "lib/media/timeline/timeline.h"
#include "lib/media/timeline/timeline_function.h"

namespace media {

VideoFrameSource::VideoFrameSource() {
  // Make sure the PTS rate for all packets is nanoseconds.
  SetPtsRate(TimelineRate::NsPerSecond);

  // We accept revised media types.
  AcceptRevisedMediaType();

  timeline_control_point_.SetProgramRangeSetCallback(
      [this](uint64_t program, int64_t min_pts, int64_t max_pts) {
        FXL_DCHECK(program == 0) << "Non-zero program not implemented";
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
}

VideoFrameSource::~VideoFrameSource() {
  // Close the bindings before members are destroyed so we don't try to
  // destroy any callbacks that are pending on open channels.
  timeline_control_point_.Reset();
}

void VideoFrameSource::AdvanceReferenceTime(int64_t reference_time) {
  uint32_t generation;
  timeline_control_point_.SnapshotCurrentFunction(
      reference_time, &current_timeline_function_, &generation);

  pts_ = current_timeline_function_(reference_time);

  DiscardOldPackets();
}

void VideoFrameSource::GetRgbaFrame(uint8_t* rgba_buffer,
                                    const geometry::Size& rgba_buffer_size) {
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

void VideoFrameSource::OnPacketSupplied(
    std::unique_ptr<SuppliedPacket> supplied_packet) {
  FXL_DCHECK(supplied_packet);
  FXL_DCHECK(supplied_packet->packet().pts_rate_ticks ==
             TimelineRate::NsPerSecond.subject_delta());
  FXL_DCHECK(supplied_packet->packet().pts_rate_seconds ==
             TimelineRate::NsPerSecond.reference_delta());

  if (supplied_packet->packet().flags & kFlagEos) {
    if (prime_callback_) {
      // We won't get any more packets, so we're as primed as we're going to
      // get.
      prime_callback_();
      prime_callback_ = nullptr;
    }

    timeline_control_point_.SetEndOfStreamPts(supplied_packet->packet().pts);
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

  if (supplied_packet->packet().pts < min_pts_) {
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
                                        FlushCallback callback) {
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
  FXL_DCHECK(packet);

  const MediaTypePtr& revised_media_type = packet->revised_media_type;

  if (revised_media_type && revised_media_type->details &&
      revised_media_type->details->get_video()) {
    converter_.SetStreamType(
        fxl::To<std::unique_ptr<StreamType>>(revised_media_type));

    if (stream_type_revised_callback_) {
      stream_type_revised_callback_();
    }
  }
}

}  // namespace media
