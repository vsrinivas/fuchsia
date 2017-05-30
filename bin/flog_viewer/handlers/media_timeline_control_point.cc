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
  terse_out() << entry() << "MediaTimelineControlPoint.BoundAs" << std::endl;
  terse_out() << indent;
  terse_out() << begl << "koid: " << AsKoid(koid) << std::endl;
  terse_out() << outdent;

  BindAs(koid);
}

void MediaTimelineControlPoint::PrimeRequested() {
  terse_out() << entry() << "MediaTimelineControlPoint.PrimeRequested"
              << std::endl;

  accumulator_->prime_requests_.Add();
}

void MediaTimelineControlPoint::CompletingPrime() {
  terse_out() << entry() << "MediaTimelineControlPoint.CompletingPrime"
              << std::endl;

  accumulator_->prime_requests_.Remove();
}

void MediaTimelineControlPoint::ScheduleTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  terse_out() << entry()
              << "MediaTimelineControlPoint.ScheduleTimelineTransform"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "timeline_transform: " << timeline_transform
              << std::endl;
  terse_out() << outdent;

  accumulator_->timeline_updates_.Add();
  accumulator_->pending_timeline_transform_ = std::move(timeline_transform);
}

void MediaTimelineControlPoint::ApplyTimelineTransform(
    media::TimelineTransformPtr timeline_transform) {
  terse_out() << entry() << "MediaTimelineControlPoint.ApplyTimelineTransform"
              << std::endl;
  terse_out() << indent;
  terse_out() << begl << "timeline_transform: " << timeline_transform
              << std::endl;
  terse_out() << outdent;

  accumulator_->timeline_updates_.Remove();
  accumulator_->current_timeline_transform_ = std::move(timeline_transform);
  accumulator_->pending_timeline_transform_ = nullptr;
}

void MediaTimelineControlPoint::ReachedEndOfStream() {
  terse_out() << entry() << "MediaTimelineControlPoint.ReachedEndOfStream"
              << std::endl;

  accumulator_->end_of_streams_reached_.Remove();
}

MediaTimelineControlPointAccumulator::MediaTimelineControlPointAccumulator() {}

MediaTimelineControlPointAccumulator::~MediaTimelineControlPointAccumulator() {}

void MediaTimelineControlPointAccumulator::Print(std::ostream& os) {
  os << "MediaTimelineControlPoint" << std::endl;
  os << indent;
  os << begl << "timeline updates: " << timeline_updates_.count() << std::endl;

  os << begl << "current timeline transform: " << current_timeline_transform_
     << std::endl;

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

  os << begl << "end-of-streams reached: " << end_of_streams_reached_.count();

  Accumulator::Print(os);
  os << outdent;
}

}  // namespace handlers
}  // namespace flog
