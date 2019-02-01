// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_STROKE_STROKE_FITTER_H_
#define GARNET_BIN_UI_SKETCHY_STROKE_STROKE_FITTER_H_

#include "garnet/bin/ui/sketchy/stroke/stroke_path.h"

#include <vector>
#include "garnet/public/lib/fxl/macros.h"
#include "third_party/glm/glm/vec2.hpp"

namespace sketchy_service {

class StrokePath;

// Wraps around StrokePath and generates the fitted path into it.
class StrokeFitter final {
 public:
  explicit StrokeFitter(glm::vec2 start_pt);
  void Extend(const std::vector<glm::vec2>& sampled_pts);

  // Fits the entire curve, populates the path, and pops the points that are
  // stable enough. Returns if the path is stable or not.
  bool FitAndPop(StrokePath* path);

 private:
  void FitSampleRange(int start_index, int end_index, glm::vec2 left_tangent,
                      glm::vec2 right_tangent, StrokePath* path);

  std::vector<glm::vec2> points_;
  std::vector<float> params_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeFitter);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_STROKE_STROKE_FITTER_H_
