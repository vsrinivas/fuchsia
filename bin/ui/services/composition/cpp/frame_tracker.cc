// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/composition/cpp/frame_tracker.h"

#include "lib/ftl/logging.h"

namespace mozart {

FrameTracker::FrameTracker() {}

FrameTracker::~FrameTracker() {}

void FrameTracker::Clear() {
  frame_count_ = 0u;
  frame_info_ = FrameInfo();
  presentation_time_delta_ = ftl::TimeDelta::Zero();
}

void FrameTracker::Update(const FrameInfo& raw_frame_info, ftl::TimePoint now) {
  const int64_t now_ticks = now.ToEpochDelta().ToNanoseconds();
  const int64_t old_base_time = frame_info_.base_time;
  const int64_t old_presentation_time = frame_info_.presentation_time;
  frame_info_ = raw_frame_info;

  // Ensure frame info is sane since it comes from another service.
  if (frame_info_.base_time > now_ticks) {
    FTL_LOG(WARNING) << "Frame time is in the future: base_time="
                     << frame_info_.base_time << ", now=" << now_ticks;
    frame_info_.base_time = now_ticks;
  }
  if (frame_info_.publish_deadline < frame_info_.base_time) {
    FTL_LOG(WARNING)
        << "Publish deadline is earlier than base time: publish_deadline="
        << frame_info_.publish_deadline
        << ", base_time=" << frame_info_.base_time << ", now=" << now_ticks;
    frame_info_.publish_deadline = frame_info_.base_time;
  }
  if (frame_info_.presentation_time < frame_info_.publish_deadline) {
    FTL_LOG(WARNING) << "Presentation time is earlier than publish deadline: "
                        "presentation_time="
                     << frame_info_.presentation_time
                     << ", publish_deadline=" << frame_info_.publish_deadline
                     << ", now=" << now_ticks;
    frame_info_.presentation_time = frame_info_.publish_deadline;
  }

  // Compensate for significant lag by adjusting the base time if needed
  // to step past skipped frames.
  uint64_t lag = now_ticks - frame_info_.base_time;
  if (frame_info_.presentation_interval > 0u &&
      lag >= frame_info_.presentation_interval) {
    uint64_t offset = lag % frame_info_.presentation_interval;
    uint64_t adjustment = now_ticks - offset - frame_info_.base_time;
    frame_info_.base_time = now_ticks - offset;
    frame_info_.publish_deadline += adjustment;
    frame_info_.presentation_time += adjustment;

    // Jank warning.
    // TODO(jeffbrown): Suppress this once we're happy with things.
    FTL_VLOG(1) << "Missed " << (frame_info_.presentation_interval * 0.000001f)
                << " ms publish deadline by " << (lag * 0.000001f)
                << " ms, skipping " << (lag / frame_info_.presentation_interval)
                << " frames";
  }

  // Ensure monotonicity.
  if (frame_count_++ == 0u)
    return;
  if (frame_info_.base_time < old_base_time) {
    FTL_LOG(WARNING) << "Frame time is going backwards: new="
                     << frame_info_.base_time << ", old=" << old_base_time
                     << ", now=" << now_ticks;
    frame_info_.base_time = old_base_time;
  }
  if (frame_info_.presentation_time < old_presentation_time) {
    FTL_LOG(WARNING) << "Presentation time is going backwards: new="
                     << frame_info_.presentation_time
                     << ", old=" << old_presentation_time
                     << ", now=" << now_ticks;
    frame_info_.presentation_time = old_presentation_time;
  }
  presentation_time_delta_ =
      ftl::TimeDelta::FromNanoseconds(frame_info_.base_time - old_base_time);
}

}  // namespace mozart
