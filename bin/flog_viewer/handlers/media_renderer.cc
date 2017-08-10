// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_renderer.h"

#include <algorithm>
#include <iostream>

#include "apps/media/lib/timeline/fidl_type_conversions.h"
#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer.h"
#include "apps/media/tools/flog_viewer/handlers/media_timeline_control_point.h"

namespace flog {
namespace handlers {

MediaRenderer::MediaRenderer(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaRendererAccumulator>()) {
  stub_.set_sink(this);
}

MediaRenderer::~MediaRenderer() {}

void MediaRenderer::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaRenderer::GetAccumulator() {
  return accumulator_;
}

void MediaRenderer::BoundAs(uint64_t koid) {
  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaRenderer.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaRenderer::Config(fidl::Array<media::MediaTypeSetPtr> supported_types,
                           uint64_t consumer_address,
                           uint64_t timeline_control_point_address) {
  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaRenderer.Config\n";
  terse_out() << indent;
  terse_out() << begl << "supported_types: " << supported_types << "\n";
  terse_out() << begl << "consumer_address: " << *AsChannel(consumer_address)
              << "\n";
  terse_out() << outdent;

  FTL_DCHECK(supported_types);
  FTL_DCHECK(consumer_address);

  accumulator_->supported_types_ = std::move(supported_types);
  accumulator_->consumer_channel_ = AsChannel(consumer_address);
  accumulator_->consumer_channel_->SetHasParent();
  accumulator_->timeline_control_point_channel_ =
      AsChannel(timeline_control_point_address);
  accumulator_->timeline_control_point_channel_->SetHasParent();
}

void MediaRenderer::SetMediaType(media::MediaTypePtr type) {
  terse_out() << EntryHeader(entry(), entry_index())
              << "MediaRenderer.SetMediaType\n";
  terse_out() << indent;
  terse_out() << begl << "type: " << type << "\n";
  terse_out() << outdent;

  FTL_DCHECK(type);

  if (type->encoding == media::MediaType::kAudioEncodingLpcm) {
    FTL_DCHECK(type->details);
    FTL_DCHECK(type->details->is_audio());
    media::AudioMediaTypeDetails* details = type->details->get_audio().get();
    FTL_DCHECK(details != nullptr);

    audio_frame_rate_ = media::TimelineRate(details->frames_per_second, 1);

    switch (details->sample_format) {
      case media::AudioSampleFormat::UNSIGNED_8:
        audio_frame_size_ = details->channels * sizeof(uint8_t);
        break;
      case media::AudioSampleFormat::SIGNED_16:
        audio_frame_size_ = details->channels * sizeof(uint16_t);
        break;
      case media::AudioSampleFormat::SIGNED_24_IN_32:
        audio_frame_size_ = details->channels * sizeof(uint32_t);
        break;
      case media::AudioSampleFormat::FLOAT:
        audio_frame_size_ = details->channels * sizeof(float);
        break;
      default:
        ReportProblem() << "Unrecognized sample type "
                        << static_cast<int>(details->sample_format);
        audio_frame_size_ = 0;
        break;
    }
  }

  accumulator_->type_ = std::move(type);
}

void MediaRenderer::PtsRate(uint32_t ticks, uint32_t seconds) {
  terse_out() << EntryHeader(entry(), entry_index()) << "MediaRenderer.PtsRate"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "ticks: " << ticks << std::endl;
  terse_out() << begl << "seconds: " << seconds << std::endl;
  terse_out() << outdent;

  accumulator_->pts_rate_ = media::TimelineRate(ticks, seconds);
}

void MediaRenderer::EngagePacket(int64_t current_pts,
                                 int64_t packet_pts,
                                 uint64_t packet_label) {
  full_out() << EntryHeader(entry(), entry_index())
             << "MediaRenderer.EngagePacket\n";
  full_out() << indent;
  full_out() << begl << "current_pts: " << AsNsTime(current_pts) << "\n";
  full_out() << begl << "packet_pts: " << AsNsTime(packet_pts) << "\n";
  full_out() << begl << "packet_label: " << packet_label << "\n";
  full_out() << outdent;

  if (packet_label == 0) {
    // Needed a packet but there was none.
    accumulator_->starved_no_packet_.Add();
    ReportProblem() << "Renderer starved, no packet";
    return;
  }

  if (packet_pts < current_pts) {
    // Needed a packet, but the newest one was too old.
    accumulator_->starved_ns_.Add(
        static_cast<uint64_t>((current_pts - packet_pts) *
                              media::TimelineRate::Product(
                                  media::TimelineRate::NsPerSecond,
                                  accumulator_->pts_rate_.Inverse(), false)));
    ReportProblem() << "Renderer starved, stale packet";
    return;
  }

  // Get the timeline control point for timing information.
  MediaTimelineControlPoint* timeline_control_point = GetTimelineControlPoint();
  if (timeline_control_point == nullptr) {
    return;
  }

  if (!timeline_control_point->current_timeline_transform() ||
      timeline_control_point->current_timeline_transform()->subject_delta ==
          0) {
    // Engaged packet as part of preroll (while paused).
    accumulator_->preroll_packets_.Add();
    return;
  }

  // Engaged packet while playing. The consumer should have the packet.
  MediaPacketConsumer* consumer = GetConsumer();
  if (consumer == nullptr) {
    return;
  }

  std::shared_ptr<MediaPacketConsumerAccumulator::Packet> packet =
      consumer->FindOutstandingPacket(packet_label);
  if (packet != nullptr) {
    RecordPacketEarliness(packet.get(), timeline_control_point);
  } else {
    // Couldn't find the packet. This shouldn't happen.
    accumulator_->missing_packets_.Add();
  }
}

void MediaRenderer::RenderRange(int64_t pts, uint32_t duration) {
  full_out() << EntryHeader(entry(), entry_index())
             << "MediaRenderer.RenderRange" << std::endl;
  full_out() << indent;
  full_out() << begl << "pts: " << AsNsTime(pts) << std::endl;
  full_out() << begl << "duration: " << duration << std::endl;
  full_out() << outdent;

  if (audio_frame_rate_ == media::TimelineRate::Zero) {
    ReportProblem() << "RenderRange called for non-audio media type";
    return;
  }

  if (audio_frame_size_ == 0) {
    return;
  }

  // Get the timeline control point for timing information.
  MediaTimelineControlPoint* timeline_control_point = GetTimelineControlPoint();
  if (timeline_control_point == nullptr) {
    return;
  }

  if (!timeline_control_point->current_timeline_transform() ||
      timeline_control_point->current_timeline_transform()->subject_delta ==
          0) {
    // Rendered range while paused.
    accumulator_->preroll_renders_.Add();
    was_paused_ = true;
    return;
  }

  // Get the consumer, which has a collection of outstanding packets.
  MediaPacketConsumer* consumer = GetConsumer();
  if (consumer == nullptr) {
    return;
  }

  // Make sure |pts| and |duration| are in frames/second.
  if (accumulator_->pts_rate_ != audio_frame_rate_) {
    media::TimelineRate conversion = media::TimelineRate::Product(
        audio_frame_rate_, accumulator_->pts_rate_.Inverse(), false);
    pts = pts * conversion;
    duration = duration * conversion;
  }

  if (expected_range_pts_ != media::MediaPacket::kNoTimestamp &&
      pts != expected_range_pts_) {
    int64_t diff;
    if (pts > expected_range_pts_) {
      diff = static_cast<int64_t>(pts - expected_range_pts_);
    } else {
      diff = -static_cast<int64_t>(expected_range_pts_ - pts);
    }

    // Off-by-one errors are expected, because the 'real' duration is typically
    // not an integer.
    if (diff > 1 || diff < -1) {
      ReportProblem() << "Unexpected RenderRange pts: expected "
                      << AsNsTime(expected_range_pts_) << ", got "
                      << AsNsTime(pts) << ", diff " << diff;
    }
  }

  bool more_packets = false;

  for (auto pair : consumer->outstanding_packets()) {
    std::shared_ptr<MediaPacketConsumerAccumulator::Packet> packet =
        pair.second;

    RecordPacketEarliness(packet.get(), timeline_control_point);

    int64_t packet_pts = packet->packet_->pts;
    media::TimelineRate packet_pts_rate(packet->packet_->pts_rate_ticks,
                                        packet->packet_->pts_rate_seconds);
    if (packet_pts_rate != audio_frame_rate_) {
      packet_pts =
          packet_pts * media::TimelineRate::Product(
                           audio_frame_rate_, packet_pts_rate.Inverse(), false);
    }

    if (packet->packet_->payload_size / audio_frame_size_ >
        std::numeric_limits<uint32_t>::max()) {
      ReportProblem() << "Absurd payload size " << packet->packet_->payload_size
                      << ", packet label " << packet->label_;
      return;
    }

    uint32_t packet_duration = static_cast<uint32_t>(
        packet->packet_->payload_size / audio_frame_size_);

    if (packet_pts + packet_duration <= pts) {
      // Packet occurs before the range.
      continue;
    }

    if (pts + duration <= packet_pts) {
      // Packet occurs after the range.
      more_packets = true;
      break;
    }

    if (pts < packet_pts) {
      // We've found a gap.
      FTL_DCHECK(packet_pts - pts <= std::numeric_limits<uint32_t>::max());
      uint32_t gap_size = static_cast<uint32_t>(packet_pts - pts);
      FTL_DCHECK(gap_size <= duration);

      if (was_paused_) {
        accumulator_->gaps_in_frames_before_first_.Add(gap_size);
      } else {
        ReportProblem() << "Gap of " << gap_size
                        << " audio frames (between packets) at pts "
                        << AsNsTime(pts);
        accumulator_->gaps_in_frames_between_packets_.Add(gap_size);
      }

      pts += gap_size;
      duration -= gap_size;
    } else {
      was_paused_ = false;
    }

    uint32_t advance = std::min(duration, packet_duration);
    pts += advance;
    duration -= advance;
  }

  if (duration != 0) {
    if (was_paused_ && more_packets) {
      accumulator_->gaps_in_frames_before_first_.Add(duration);
    } else if (more_packets) {
      ReportProblem() << "Gap of " << duration
                      << " audio frames (between packets) at pts "
                      << AsNsTime(pts);
      accumulator_->gaps_in_frames_between_packets_.Add(duration);
    } else if (end_of_stream_) {
      accumulator_->gaps_in_frames_end_of_stream_.Add(duration);
    } else {
      ReportProblem() << "Gap of " << duration
                      << " audio frames (no packet) at pts " << AsNsTime(pts);
      accumulator_->gaps_in_frames_no_packet_.Add(duration);
    }
  }

  expected_range_pts_ = pts + duration;
}

MediaTimelineControlPoint* MediaRenderer::GetTimelineControlPoint() {
  if (!accumulator_->timeline_control_point_channel_) {
    return nullptr;
  }

  return reinterpret_cast<MediaTimelineControlPoint*>(
      accumulator_->timeline_control_point_channel_->handler().get());
}

MediaPacketConsumer* MediaRenderer::GetConsumer() {
  if (!accumulator_->consumer_channel_) {
    return nullptr;
  }

  return reinterpret_cast<MediaPacketConsumer*>(
      accumulator_->consumer_channel_->handler().get());
}

void MediaRenderer::RecordPacketEarliness(
    MediaPacketConsumerAccumulator::Packet* packet,
    MediaTimelineControlPoint* timeline_control_point) {
  FTL_DCHECK(packet != nullptr);
  FTL_DCHECK(timeline_control_point != nullptr);

  if (packet->label_ <= earliness_prev_packet_label_) {
    // Already added this one.
    return;
  }

  end_of_stream_ = packet->packet_->end_of_stream;

  earliness_prev_packet_label_ = packet->label_;

  media::TimelineFunction presentation_timeline =
      timeline_control_point->current_timeline_transform()
          .To<media::TimelineFunction>();

  int64_t packet_pts_ns =
      packet->packet_->pts *
      media::TimelineRate::Product(
          media::TimelineRate::NsPerSecond,
          media::TimelineRate(packet->packet_->pts_rate_seconds,
                              packet->packet_->pts_rate_ticks),
          false);

  // Calculate the reference time corresponding to the pts.
  int64_t packet_presentation_reference_time =
      presentation_timeline.ApplyInverse(packet_pts_ns);

  if (packet_presentation_reference_time > packet->time_ns_) {
    // Track the delta between arrival and presentation.
    accumulator_->packet_earliness_ns_.Add(packet_presentation_reference_time -
                                           packet->time_ns_);
  }
}

MediaRendererAccumulator::MediaRendererAccumulator() {}

MediaRendererAccumulator::~MediaRendererAccumulator() {}

void MediaRendererAccumulator::Print(std::ostream& os) {
  os << "MediaRenderer\n";
  os << indent;
  os << begl << "supported_types: " << supported_types_ << "\n";

  if (consumer_channel_) {
    os << begl << "consumer: " << *consumer_channel_ << " ";
    FTL_DCHECK(consumer_channel_->resolved());
    consumer_channel_->PrintAccumulator(os);
    os << "\n";
  } else {
    os << begl << "consumer: <none>\n";
  }

  if (timeline_control_point_channel_) {
    os << begl << "timeline control point: " << *timeline_control_point_channel_
       << " ";
    FTL_DCHECK(timeline_control_point_channel_->resolved());
    timeline_control_point_channel_->PrintAccumulator(os);
    os << "\n";
  } else {
    os << begl << "timeline control point:: <none>\n";
  }

  os << begl << "type: " << type_ << "\n";

  os << begl << "pts rate: " << pts_rate_ << std::endl;

  if (preroll_packets_.count() != 0) {
    os << begl << "preroll packets: " << preroll_packets_.count() << "\n";
  }

  if (preroll_renders_.count() != 0) {
    os << begl << "preroll renders: " << preroll_renders_.count() << std::endl;
  }

  os << begl << "packet earliness: min " << AsNsTime(packet_earliness_ns_.min())
     << ", avg " << AsNsTime(packet_earliness_ns_.average()) << ", max "
     << AsNsTime(packet_earliness_ns_.max());

  if (starved_no_packet_.count() != 0) {
    os << "\n" << begl << "STARVED (no packet): " << starved_no_packet_.count();
  }

  if (starved_ns_.count() != 0) {
    os << "\n"
       << begl << "STARVED (stale packet): count " << starved_ns_.count()
       << ", staleness min " << AsNsTime(starved_ns_.min()) << ", avg "
       << AsNsTime(starved_ns_.average()) << ", max "
       << AsNsTime(starved_ns_.max());
  }

  if (missing_packets_.count() != 0) {
    os << "\n" << begl << "PACKETS NOT FOUND: " << missing_packets_.count();
  }

  if (gaps_in_frames_before_first_.count() != 0) {
    os << std::endl
       << begl << "gaps due to initial pts: count "
       << gaps_in_frames_before_first_.count() << ", duration in frames min "
       << gaps_in_frames_before_first_.min() << ", avg "
       << gaps_in_frames_before_first_.average() << ", max "
       << gaps_in_frames_before_first_.max();
  }

  if (gaps_in_frames_end_of_stream_.count() != 0) {
    os << std::endl
       << begl << "renders after end-of-stream: count "
       << gaps_in_frames_end_of_stream_.count() << ", duration in frames min "
       << gaps_in_frames_end_of_stream_.min() << ", avg "
       << gaps_in_frames_end_of_stream_.average() << ", max "
       << gaps_in_frames_end_of_stream_.max();
  }

  if (gaps_in_frames_no_packet_.count() != 0) {
    os << std::endl
       << begl << "STARVED (audio gap, no packet): count "
       << gaps_in_frames_no_packet_.count() << ", gap size in frames min "
       << gaps_in_frames_no_packet_.min() << ", avg "
       << gaps_in_frames_no_packet_.average() << ", max "
       << gaps_in_frames_no_packet_.max();
  }

  if (gaps_in_frames_between_packets_.count() != 0) {
    os << std::endl
       << begl << "STARVED (audio gap between packets): count "
       << gaps_in_frames_between_packets_.count() << ", gap size in frames min "
       << gaps_in_frames_between_packets_.min() << ", avg "
       << gaps_in_frames_between_packets_.average() << ", max "
       << gaps_in_frames_between_packets_.max();
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
