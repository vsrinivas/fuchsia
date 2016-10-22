// Copyright 2015 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef APPS_MOZART_SRC_COMPOSITOR_FRAME_INFO_H_
#define APPS_MOZART_SRC_COMPOSITOR_FRAME_INFO_H_

#include "lib/ftl/time/time_delta.h"
#include "lib/ftl/time/time_point.h"

namespace compositor {

// Typed variant of |mozart::FrameInfo|.
struct FrameInfo {
  // Time when the frame will be presented.
  ftl::TimePoint presentation_time;

  // Nominal interval between successive frames.
  ftl::TimeDelta presentation_interval;

  // Deadline for publishing new scene state for the frame.
  ftl::TimePoint publish_deadline;

  // When then compositor started working on the frame.
  ftl::TimePoint base_time;
};

}  // namespace compositor

#endif  // APPS_MOZART_SRC_COMPOSITOR_FRAME_INFO_H_
