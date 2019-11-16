// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/scheduling/constant_frame_predictor.h"

#include <trace/event.h>

namespace scheduling {

ConstantFramePredictor::ConstantFramePredictor(zx::duration static_vsync_offset)
    : vsync_offset_(static_vsync_offset) {}

ConstantFramePredictor::~ConstantFramePredictor() {}

PredictedTimes ConstantFramePredictor::GetPrediction(PredictionRequest request) {
  // Pretty print the times in milliseconds.
  TRACE_INSTANT("gfx", "ConstantFramePredictor::GetPrediction", TRACE_SCOPE_PROCESS,
                "Predicted frame duration(ms)",
                static_cast<double>(vsync_offset_.to_usecs()) / 1000);
  return FramePredictor::ComputePredictionFromDuration(request, vsync_offset_);
}

void ConstantFramePredictor::ReportRenderDuration(zx::duration time_to_render) {}

void ConstantFramePredictor::ReportUpdateDuration(zx::duration time_to_update) {}

}  // namespace scheduling
