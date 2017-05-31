// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/media/tools/flog_viewer/handlers/media_timeline_control_point.h"

#include <iostream>

#include "apps/media/lib/timeline/timeline_function.h"
#include "apps/media/lib/timeline/timeline_rate.h"
#include "apps/media/services/logs/media_timeline_control_point_channel.fidl.h"
#include "apps/media/tools/flog_viewer/flog_viewer.h"
#include "apps/media/tools/flog_viewer/handlers/media_formatting.h"

namespace flog {
namespace handlers {

MediaTimelineControlPoint::MediaTimelineControlPoint(const std::string& format)
    : ChannelHandler(format),
      accumulator_(std::make_shared<MediaTimelineControlPointAccumulator>()) {
  stub_.set_sink(this);
}

MediaTimelineControlPoint::~MediaTimelineControlPoint() {}

void MediaTimelineControlPoint::HandleMessage(fidl::Message* message) {
  stub_.Accept(message);
}

std::shared_ptr<Accumulator> MediaTimelineControlPoint::GetAccumulator() {
  return accumulator_;
}

void MediaTimelineControlPoint::BoundAs(uint64_t koid) {
  terse_out() << entry() << "MediaTimelineControlPoint.BoundAs\n";
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << "\n";
  terse_out() << outdent;

  BindAs(koid);
}

void MediaTimelineControlPoint::PrimeRequested() {
  terse_out() << entry() << "MediaTimelineControlPoint.PrimeRequested"
              << "\n";

  accumulator_->prime_requests_.Add();
}

void MediaTimelineControlPoint::CompletingPrime() {
  terse_out() << entry() << "MediaTimelineControlPoint.CompletingPrime"
              << "\n";

  accumulator_->prime_requests_.Remove();
}

void MediaTimelineControlPoint::ScheduleTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  terse_out() << entry()
              << "MediaTimelineControlPoint.ScheduleTimelineTransform"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "timeline_transform: " << timeline_transform << "\n";
  terse_out() << outdent;

  accumulator_->timeline_updates_.Add();
  accumulator_->pending_timeline_transform_ = std::move(timeline_transform);
}

void MediaTimelineControlPoint::ApplyTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  terse_out() << entry() << "MediaTimelineControlPoint.ApplyTimelineTransform"
              << "\n";
  terse_out() << indent;
  terse_out() << begl << "timeline_transform: " << timeline_transform << "\n";
  terse_out() << outdent;

  accumulator_->timeline_updates_.Remove();
  accumulator_->current_timeline_transform_ = std::move(timeline_transform);
  accumulator_->pending_timeline_transform_ = nullptr;
}

void MediaTimelineControlPoint::ReachedEndOfStream() {
  terse_out() << entry() << "MediaTimelineControlPoint.ReachedEndOfStream"
              << "\n";

  accumulator_->end_of_streams_reached_.Remove();
}

MediaTimelineControlPointAccumulator::MediaTimelineControlPointAccumulator() {}

MediaTimelineControlPointAccumulator::~MediaTimelineControlPointAccumulator() {}

void MediaTimelineControlPointAccumulator::Print(std::ostream& os) {
  os << "MediaTimelineControlPoint\n";
  os << indent;
  os << begl << "timeline updates: " << timeline_updates_.count() << "\n";

  os << begl << "current timeline transform: " << current_timeline_transform_
     << "\n";

  if (pending_timeline_transform_) {
    os << begl
       << "SUSPENSE: pending timeline update: " << pending_timeline_transform_
       << "\n";
  }

  os << begl << "prime requests: " << prime_requests_.count() << "\n";
  if (prime_requests_.outstanding_count() == 1) {
    os << begl << "SUSPENSE: prime request outstanding\n";
  } else if (prime_requests_.outstanding_count() > 1) {
    // There should by at most one outstanding prime request.
    os << begl << "PROBLEM: prime requests outstanding: "
       << prime_requests_.outstanding_count() << "\n";
  }

  os << begl << "end-of-streams reached: " << end_of_streams_reached_.count();

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
