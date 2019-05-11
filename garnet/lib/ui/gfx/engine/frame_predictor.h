// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_ENGINE_FRAME_PREDICTOR_H_
#define GARNET_LIB_UI_GFX_ENGINE_FRAME_PREDICTOR_H_

#include <lib/zx/time.h>

#include <vector>

#include "garnet/lib/ui/gfx/engine/duration_predictor.h"

namespace scenic_impl {
namespace gfx {

struct PredictedTimes {
  // The point at which a client should begin an update and render a frame,
  // so that it is done by the |presentation_time|.
  zx::time latch_point_time;
  // The predicted presentation time. This corresponds to a future VSYNC.
  zx::time presentation_time;
};

struct PredictionRequest {
  zx::time now;
  // The minimum presentation time a client would like to hit.
  zx::time requested_presentation_time;
  zx::time last_vsync_time;
  zx::duration vsync_interval;
};

// Predicts viable presentation times and corresponding latch-points for a
// frame, based on previously reported update and render durations.
class FramePredictor {
 public:
  FramePredictor(zx::duration initial_render_duration_prediction,
                 zx::duration initial_update_duration_prediction);
  ~FramePredictor() = default;

  // Computes the target presentation time for
  // |request.requested_presentation_time|, and a latch-point that is early
  // enough to apply one update and render a frame, in order to hit the
  // predicted presentation time.
  //
  // Both |PredictedTimes.latch_point_time| and |PredictedTimes.presentation_time|
  // are guaranteed to be after |request.now|.
  // |PredictedTimes.presentation_time| is guaranteed to be later than or equal
  // to |request.requested_presentation_time|.
  PredictedTimes GetPrediction(PredictionRequest request) const;

  // Used by the client to report a measured render duration. The render
  // duration is the CPU + GPU time it takes to build and render a frame. This
  // will be considered in subsequent calls to |GetPrediction|.
  void ReportRenderDuration(zx::duration time_to_render);

  // Used by the client to report a measured update duration. The update
  // duration is the time it takes to apply a batch of updates. This will be
  // considered in subsequent calls to |GetPrediction|.
  void ReportUpdateDuration(zx::duration time_to_update);

 private:
  // Returns the next time to synchronize to.
  // |last_sync_time| The last known good sync time.
  // |sync_interval| The expected time between syncs.
  // |min_sync_time| The minimum time allowed to return.
  static zx::time ComputeNextSyncTime(zx::time last_sync_time,
                                       zx::duration sync_interval,
                                       zx::time min_sync_time);
  // Returns a prediction for how long in total the next frame will take to
  // update and render.
  zx::duration PredictTotalRequiredDuration() const;

  // Safety margin added to prediction time to reduce impact of noise and
  // misprediction. Unfortunately means minimum possible latency is increased
  // by the same amount.
  const zx::duration kHardcodedMargin = zx::usec(500);  // 0.5ms

  // Render time prediction.
  const size_t kRenderPredictionWindowSize = 3;
  DurationPredictor render_duration_predictor_;

  // Update time prediction.
  const size_t kUpdatePredictionWindowSize = 1;
  DurationPredictor update_duration_predictor_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_ENGINE_FRAME_PREDICTOR_H_
