// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_renderer_digest.h"

#include <iostream>

#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/services/logs/media_renderer_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"
#include "apps/media/tools/flog_viewer/handlers/media_packet_consumer_digest.h"

namespace flog {
namespace handlers {

MediaRendererDigest::MediaRendererDigest(const std::string& format)
    : accumulator_(std::make_shared<MediaRendererAccumulator>()) {
  FTL_DCHECK(format == FlogViewer::kFormatDigest);
  stub_.set_sink(this);
}

MediaRendererDigest::~MediaRendererDigest() {}

void MediaRendererDigest::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaRendererDigest::GetAccumulator() {
  return accumulator_;
}

void MediaRendererDigest::BoundAs(uint64_t koid) {
  BindAs(koid);
}

void MediaRendererDigest::Config(
    fidl::Array<media::MediaTypeSetPtr> supported_types,
    uint64_t consumer_address) {
  FTL_DCHECK(supported_types);
  FTL_DCHECK(consumer_address);

  accumulator_->supported_types_ = std::move(supported_types);
  accumulator_->consumer_channel_ = AsChannel(consumer_address);
  accumulator_->consumer_channel_->SetHasParent();
}

void MediaRendererDigest::SetMediaType(media::MediaTypePtr type) {
  FTL_DCHECK(type);
  accumulator_->type_ = std::move(type);
}

void MediaRendererDigest::PrimeRequested() {
  accumulator_->prime_requests_.Add();
}

void MediaRendererDigest::CompletingPrime() {
  accumulator_->prime_requests_.Remove();
}

void MediaRendererDigest::ScheduleTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  accumulator_->timeline_updates_.Add();
  accumulator_->pending_timeline_transform_ = std::move(timeline_transform);
}

void MediaRendererDigest::ApplyTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  accumulator_->timeline_updates_.Remove();
  accumulator_->current_timeline_transform_ = std::move(timeline_transform);
  accumulator_->pending_timeline_transform_ = nullptr;
}

void MediaRendererDigest::EngagePacket(int64_t current_pts,
                                       int64_t packet_pts,
                                       uint64_t packet_label) {
  if (packet_label == 0) {
    // Needed a packet but there was none.
    accumulator_->starved_no_packet_.Add();
    ReportProblem() << "Renderer starved, no packet";
  } else if (packet_pts < current_pts) {
    // Needed a packet, but the newest one was too old.
    accumulator_->starved_ns_.Add(
        static_cast<uint64_t>(current_pts - packet_pts));
    ReportProblem() << "Renderer starved, stale packet";
  } else if (!accumulator_->current_timeline_transform_ ||
             accumulator_->current_timeline_transform_->subject_delta == 0) {
    // Engaged packet as part of preroll (while paused).
    accumulator_->preroll_packets_.Add();
  } else if (accumulator_->consumer_channel_) {
    // Engaged packet while playing. The consumer should have the packet.
    MediaPacketConsumerDigest* consumer_digest =
        reinterpret_cast<MediaPacketConsumerDigest*>(
            accumulator_->consumer_channel_->handler().get());
    if (consumer_digest != nullptr) {
      // Found the consumer.
      std::shared_ptr<MediaPacketConsumerAccumulator::Packet> packet =
          consumer_digest->FindOutstandingPacket(packet_label);
      if (packet != nullptr) {
        media::TimelineFunction presentation_timeline =
            accumulator_->current_timeline_transform_
                .To<media::TimelineFunction>();
        // Found the packet. Calculate the pts in nanoseconds.
        int64_t packet_pts_ns =
            packet->packet_->pts *
            media::TimelineRate::Product(
                media::TimelineRate::NsPerSecond,
                media::TimelineRate(packet->packet_->pts_rate_seconds,
                                    packet->packet_->pts_rate_ticks),
                false);

        // Now calculate the reference time corresponding to the pts.
        int64_t packet_presentation_reference_time =
            presentation_timeline.ApplyInverse(packet_pts_ns);
        FTL_DCHECK(packet_presentation_reference_time > packet->time_ns_);

        // Track the delta between arrival and presentation.
        accumulator_->packet_earliness_ns_.Add(
            packet_presentation_reference_time - packet->time_ns_);
      } else {
        // Couldn't find the packet. This shouldn't happen.
        accumulator_->missing_packets_.Add();
      }
    }
  }
}

MediaRendererAccumulator::MediaRendererAccumulator() {}

MediaRendererAccumulator::~MediaRendererAccumulator() {}

void MediaRendererAccumulator::Print(std::ostream& os) {
  os << "MediaRenderer" << std::endl;
  os << indent;
  os << begl << "supported_types: " << supported_types_;

  if (consumer_channel_) {
    os << begl << "consumer: " << *consumer_channel_ << " ";
    FTL_DCHECK(consumer_channel_->resolved());
    consumer_channel_->PrintAccumulator(os);
  } else {
    os << begl << "consumer: <none>" << std::endl;
  }

  os << begl << "type: " << type_;

  os << begl << "timeline updates: " << timeline_updates_.count() << std::endl;

  if (pending_timeline_transform_) {
    os << begl
       << "SUSPENSE: pending timeline update: " << pending_timeline_transform_
       << std::endl;
  }

  os << begl << "prime requests: " << prime_requests_.count() << std::endl;
  if (prime_requests_.outstanding_count() == 1) {
    os << begl << "SUSPENSE: prime request outstanding" << std::endl;
  } else if (prime_requests_.outstanding_count() > 1) {
    // There should by at most one outstanding prime request.
    os << begl << "PROBLEM: prime requests outstanding: "
       << prime_requests_.outstanding_count() << std::endl;
  }

  if (preroll_packets_.count() != 0) {
    os << begl << "preroll packets: " << preroll_packets_.count() << std::endl;
  }

  os << begl << "packet earliness: min " << AsTime(packet_earliness_ns_.min())
     << ", avg " << AsTime(packet_earliness_ns_.average()) << ", max "
     << AsTime(packet_earliness_ns_.max()) << std::endl;

  if (starved_no_packet_.count() != 0) {
    os << begl << "STARVED (no packet): " << starved_no_packet_.count()
       << std::endl;
  }

  if (starved_ns_.count() != 0) {
    os << begl << "STARVED (stale packet): count " << starved_ns_.count()
       << ", staleness min " << AsTime(starved_ns_.min()) << ", avg "
       << AsTime(starved_ns_.average()) << ", max " << AsTime(starved_ns_.max())
       << std::endl;
  }

  if (missing_packets_.count() != 0) {
    os << begl << "PACKETS NOT FOUND: " << missing_packets_.count()
       << std::endl;
  }

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
