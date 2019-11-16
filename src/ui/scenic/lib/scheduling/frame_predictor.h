// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_PREDICTOR_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_PREDICTOR_H_

#include <lib/zx/time.h>

#include "src/ui/scenic/lib/scheduling/duration_predictor.h"

namespace scheduling {

struct PredictedTimes {
  // The point at which a client should begin an update and render a frame,
  // so that it is done by the |presentation_time|.
  zx::time latch_point_time;
  // The predicted presentation time. This corresponds to a future VSYNC.
  zx::time presentation_time;

  constexpr bool operator==(PredictedTimes other) const {
    return latch_point_time == other.latch_point_time &&
           presentation_time == other.presentation_time;
  }

  constexpr bool operator!=(PredictedTimes other) const { return !(*this == other); }
};

struct PredictionRequest {
  zx::time now;
  // The minimum presentation time a client would like to hit.
  zx::time requested_presentation_time;
  zx::time last_vsync_time;
  zx::duration vsync_interval;
};

// Interface for performing frame predictions. Predicts viable presentation times
// and corresponding latch-points for a frame, based on previously reported update
// and render durations.
class FramePredictor {
 public:
  virtual ~FramePredictor() = default;

  // Computes the target presentation time for
  // |request.requested_presentation_time|, and a latch-point that is early
  // enough to apply one update and render a frame, in order to hit the
  // predicted presentation time.
  //
  // Both |PredictedTimes.latch_point_time| and
  // |PredictedTimes.presentation_time| are guaranteed to be after
  // |request.now|. |PredictedTimes.presentation_time| is guaranteed to be later
  // than or equal to |request.requested_presentation_time|.
  virtual PredictedTimes GetPrediction(PredictionRequest request) = 0;

  // Used by the client to report a measured render duration. The render
  // duration is the CPU + GPU time it takes to build and render a frame. This
  // will be considered in subsequent calls to |GetPrediction|.
  virtual void ReportRenderDuration(zx::duration time_to_render) = 0;

  // Used by the client to report a measured update duration. The update
  // duration is the time it takes to apply a batch of updates. This will be
  // considered in subsequent calls to |GetPrediction|.
  virtual void ReportUpdateDuration(zx::duration time_to_update) = 0;

  // ---------------------------------------------------------------------------
  // Utility functions common to all FramePredictors.
  //
  // These functions are intended to be used by specific FramePredictors when
  // calculating a prediction.
  // ---------------------------------------------------------------------------

  // Returns the |PredictionTimes| for a |PredictionRequest|, allowing for the
  // |required_frame_time|.
  static PredictedTimes ComputePredictionFromDuration(PredictionRequest request,
                                                      zx::duration required_frame_duration);

  // Returns the next time to synchronize to. Helper function for all FramePredictors.
  // |last_sync_time| The last known good sync time.
  // |sync_interval| The expected time between syncs.
  // |min_sync_time| The minimum time allowed to return.
  static zx::time ComputeNextSyncTime(zx::time last_sync_time, zx::duration sync_interval,
                                      zx::time min_sync_time);
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_FRAME_PREDICTOR_H_
