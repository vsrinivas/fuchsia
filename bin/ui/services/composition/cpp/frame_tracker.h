// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_
#define APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_

#include "apps/mozart/services/composition/interfaces/scheduling.mojom.h"
#include "lib/ftl/macros.h"
#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace mozart {

// Tracks frame scheduling information.
class FrameTracker {
 public:
  FrameTracker();
  ~FrameTracker();

  // Returns the number of frames that have been tracked.
  uint64_t frame_count() const { return frame_count_; }

  // Returns the current frame info.
  // This value is not meaningful when |frame_count()| is zero.
  const FrameInfo& frame_info() const { return frame_info_; }

  ftl::TimePoint presentation_time() const {
    return ftl::TimePoint::FromEpochDelta(
        ftl::TimeDelta::FromNanoseconds(frame_info_.presentation_time));
  }
  ftl::TimeDelta presentation_interval() const {
    return ftl::TimeDelta::FromNanoseconds(frame_info_.presentation_interval);
  }
  ftl::TimePoint publish_deadline() const {
    return ftl::TimePoint::FromEpochDelta(
        ftl::TimeDelta::FromNanoseconds(frame_info_.publish_deadline));
  }
  ftl::TimePoint base_time() const {
    return ftl::TimePoint::FromEpochDelta(
        ftl::TimeDelta::FromNanoseconds(frame_info_.base_time));
  }

  // Returns the difference between the previous presentation time and the
  // current presentation time, or 0 if this is the first frame.
  // This value is guaranteed to be non-negative.
  ftl::TimeDelta presentation_time_delta() const {
    return presentation_time_delta_;
  }

  // Clears the frame tracker's state such that the next update will be
  // treated as if it were the first.
  void Clear();

  // Updates the properties of this object with new frame scheduling
  // information from |raw_frame_info| and applies compensation for lag.
  //
  // |now| should come from a recent call to |ftl::TimePoint::Now()|.
  //
  // Whenever an application receives new frame scheduling information from the
  // system, it should call this function before using it.
  void Update(const FrameInfo& raw_frame_info, ftl::TimePoint now);

 private:
  uint64_t frame_count_ = 0u;
  FrameInfo frame_info_;
  ftl::TimeDelta presentation_time_delta_;

  FTL_DISALLOW_COPY_AND_ASSIGN(FrameTracker);
};

}  // namespace mozart

#endif  // APPS_MOZART_SERVICES_COMPOSITION_CPP_FRAME_TRACKER_H_
