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
  // TODO(MZ-269): Preserve tangent, so we can fit a smooth curve even after
  // taking the previous path and points.
  void Reset();

  const StrokePath* path() const { return path_.get(); }

 private:
  void FitSampleRange(int start_index, int end_index,
                      glm::vec2 left_tangent, glm::vec2 right_tangent);

  std::unique_ptr<StrokePath> path_;
  std::vector<glm::vec2> points_;
  std::vector<float> params_;

  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeFitter);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_STROKE_STROKE_FITTER_H_
