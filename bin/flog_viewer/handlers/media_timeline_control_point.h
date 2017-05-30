// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "apps/media/services/logs/media_timeline_control_point_channel.fidl.h"
#include "apps/media/tools/flog_viewer/accumulator.h"
#include "apps/media/tools/flog_viewer/channel_handler.h"
#include "apps/media/tools/flog_viewer/counted.h"
#include "apps/media/tools/flog_viewer/tracked.h"

namespace flog {
namespace handlers {

// Status of a media timeline control point as understood by
// MediaTimelineControlPoint.
class MediaTimelineControlPointAccumulator : public Accumulator {
 public:
  MediaTimelineControlPointAccumulator();
  ~MediaTimelineControlPointAccumulator() override;

  // Accumulator overrides.
  void Print(std::ostream& os) override;

 private:
  Counted timeline_updates_;
  media::TimelineTransformPtr pending_timeline_transform_;
  media::TimelineTransformPtr current_timeline_transform_;
  Counted prime_requests_;
  Counted end_of_streams_reached_;

  friend class MediaTimelineControlPoint;
};

// Handler for MediaTimelineControlPointChannel messages.
class MediaTimelineControlPoint
    : public ChannelHandler,
      public media::logs::MediaTimelineControlPointChannel {
 public:
  MediaTimelineControlPoint(const std::string& format);

  ~MediaTimelineControlPoint() override;

  const media::TimelineTransformPtr& current_timeline_transform() const {
    return accumulator_->current_timeline_transform_;
  }

  std::shared_ptr<Accumulator> GetAccumulator() override;

 protected:
  // ChannelHandler overrides.
  void HandleMessage(fidl::Message* message) override;

 private:
  // MediaTimelineControlPointChannel implementation.
  void BoundAs(uint64_t koid) override;

  void PrimeRequested() override;

  void CompletingPrime() override;

  void ScheduleTimelineTransform(
      media::TimelineTransformPtr timeline_transform) override;

  void ApplyTimelineTransform(
      media::TimelineTransformPtr timeline_transform) override;

  void ReachedEndOfStream() override;

 private:
  media::logs::MediaTimelineControlPointChannelStub stub_;
  std::shared_ptr<MediaTimelineControlPointAccumulator> accumulator_;
};

}  // namespace handlers
}  // namespace flog
