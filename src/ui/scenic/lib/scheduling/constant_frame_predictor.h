// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_SCHEDULING_CONSTANT_FRAME_PREDICTOR_H_
#define SRC_UI_SCENIC_LIB_SCHEDULING_CONSTANT_FRAME_PREDICTOR_H_

#include "src/ui/scenic/lib/scheduling/frame_predictor.h"

namespace scheduling {

class ConstantFramePredictor : public FramePredictor {
 public:
  ConstantFramePredictor(zx::duration static_vsync_offset);
  ~ConstantFramePredictor();

  // |FramePredictor|
  // The |PredictedTimes.latch_point| will always be a static constant from the
  // |PredictedTimes.target_presentation_time|.
  PredictedTimes GetPrediction(PredictionRequest request) override;

  // |FramePredictor|
  void ReportRenderDuration(zx::duration time_to_render) override;

  // |FramePredictor|
  void ReportUpdateDuration(zx::duration time_to_update) override;

 private:
  const zx::duration vsync_offset_;
};

}  // namespace scheduling

#endif  // SRC_UI_SCENIC_LIB_SCHEDULING_CONSTANT_FRAME_PREDICTOR_H_
