// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/windowed_frame_predictor.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>

namespace scheduling {

WindowedFramePredictor::WindowedFramePredictor(zx::duration min_predicted_frame_duration,
                                               zx::duration initial_render_duration_prediction,
                                               zx::duration initial_update_duration_prediction)
    : min_predicted_frame_duration_(min_predicted_frame_duration),
      render_duration_predictor_(kRenderPredictionWindowSize, initial_render_duration_prediction),
      update_duration_predictor_(kUpdatePredictionWindowSize, initial_update_duration_prediction) {}

WindowedFramePredictor::~WindowedFramePredictor() {}

void WindowedFramePredictor::ReportRenderDuration(zx::duration time_to_render) {
  FX_DCHECK(time_to_render >= zx::duration(0));
  render_duration_predictor_.InsertNewMeasurement(time_to_render);
}

void WindowedFramePredictor::ReportUpdateDuration(zx::duration time_to_update) {
  FX_DCHECK(time_to_update >= zx::duration(0));
  update_duration_predictor_.InsertNewMeasurement(time_to_update);
}

zx::duration WindowedFramePredictor::PredictTotalRequiredDuration() const {
  const zx::duration predicted_time_to_update = update_duration_predictor_.GetPrediction();
  const zx::duration predicted_time_to_render = render_duration_predictor_.GetPrediction();

  const zx::duration predicted_frame_duration =
      std::max(min_predicted_frame_duration_,
               std::min(kMaxPredictedFrameDuration,
                        predicted_time_to_update + predicted_time_to_render + kHardcodedMargin));

  // Pretty print the times in milliseconds.
  TRACE_INSTANT("gfx", "WindowedFramePredictor::GetPrediction", TRACE_SCOPE_PROCESS,
                "Predicted frame duration(ms)",
                static_cast<double>(predicted_frame_duration.to_usecs()) / 1000, "Render time(ms)",
                static_cast<double>(predicted_time_to_render.to_usecs()) / 1000, "Update time(ms)",
                static_cast<double>(predicted_time_to_update.to_usecs()) / 1000);

  return predicted_frame_duration;
}

PredictedTimes WindowedFramePredictor::GetPrediction(PredictionRequest request) const {
  return ComputePredictionFromDuration(request, PredictTotalRequiredDuration());
}

}  // namespace scheduling
