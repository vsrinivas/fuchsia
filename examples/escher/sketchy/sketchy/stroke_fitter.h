// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <vector>

#include "escher/escher.h"
#include "sketchy/page.h"
#include "sketchy/stroke.h"

namespace sketchy {

// Iteratively fits a piecewise cubic Bezier curve to the sampled input points.
// Generates a |Stroke| in the target |Page|, and notifies them when the stroke
// must be re-tessellated.
class StrokeFitter {
 public:
  StrokeFitter(Page* page, StrokeId id);
  ~StrokeFitter();

  void StartStroke(vec2 pt);

  void ContinueStroke(std::vector<vec2> sampled_points,
                      std::vector<vec2> predicted_points);

  void FinishStroke();

  void CancelStroke();

 private:
  void FitSampleRange(int start_index,
                      int end_index,
                      vec2 left_tangent,
                      vec2 right_tangent);

  Page* const page_;
  Stroke* const stroke_;
  const StrokeId stroke_id_;

  std::vector<vec2> points_;
  std::vector<float> params_;
  float error_threshold_;
  StrokePath path_;
  size_t predicted_point_count_ = 0;
  bool finished_ = false;
};

}  // namespace sketchy
