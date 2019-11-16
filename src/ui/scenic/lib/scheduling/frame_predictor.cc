// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/frame_predictor.h"

#include <algorithm>

namespace scheduling {

// static
zx::time FramePredictor::ComputeNextSyncTime(zx::time last_sync_time, zx::duration sync_interval,
                                             zx::time min_sync_time) {
  // If the last sync time is greater than or equal to the minimum acceptable
  // sync time, just return the last sync.
  // Note: in practice, these numbers will likely differ. The "equal to"
  // comparison is necessary for tests, which have much tighter control on time.
  if (last_sync_time >= min_sync_time) {
    return last_sync_time;
  }

  const int64_t num_intervals = (min_sync_time - last_sync_time) / sync_interval;
  return last_sync_time + (sync_interval * (num_intervals + 1));
}

// static
PredictedTimes FramePredictor::ComputePredictionFromDuration(PredictionRequest request,
                                                             zx::duration required_frame_duration) {
  // Calculate minimum time this would sync to. It is last vsync time plus half
  // a vsync-interval (to allow for jitter for the VSYNC signal), or the current
  // time plus the expected render time, whichever is larger, so we know we have
  // enough time to render for that sync.
  zx::time min_sync_time = std::max((request.last_vsync_time + (request.vsync_interval / 2)),
                                    (request.now + required_frame_duration));
  const zx::time target_vsync_time =
      ComputeNextSyncTime(request.last_vsync_time, request.vsync_interval, min_sync_time);

  // Ensure the requested presentation time is current.
  zx::time target_presentation_time = std::max(request.requested_presentation_time, request.now);

  // Compute the next presentation time from the target vsync time (inclusive),
  // that is at least the current requested present time.
  target_presentation_time =
      ComputeNextSyncTime(target_vsync_time, request.vsync_interval, target_presentation_time);

  // Find time the client should latch and start rendering in order to
  // frame in time for the target present.
  zx::time latch_point = target_presentation_time - required_frame_duration;

  return {.latch_point_time = latch_point, .presentation_time = target_presentation_time};
}

}  // namespace scheduling
