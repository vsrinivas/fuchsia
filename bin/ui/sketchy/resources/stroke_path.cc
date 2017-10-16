// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_path.h"

namespace sketchy_service {

StrokePath::StrokePath(sketchy::StrokePathPtr path) {
  Reset(path->segments.size());
  for (const auto& seg : path->segments) {
    AddCurve(sketchy::CubicBezier2f{{
        {seg->pt0->x, seg->pt0->y},
        {seg->pt1->x, seg->pt1->y},
        {seg->pt2->x, seg->pt2->y},
        {seg->pt3->x, seg->pt3->y}}});
  }
}

void StrokePath::AddCurve(sketchy::CubicBezier2f curve) {
  auto pair = curve.ArcLengthParameterization();
  control_points_.push_back(curve);
  re_params_.push_back(pair.first);
  segment_lengths_.push_back(pair.second);
  cumulative_lengths_.push_back(length_);
  length_ += pair.second;
}

void StrokePath::Reset(size_t size) {
  control_points_.clear();
  control_points_.reserve(size);
  re_params_.clear();
  re_params_.reserve(size);
  segment_lengths_.clear();
  segment_lengths_.reserve(size);
  cumulative_lengths_.clear();
  cumulative_lengths_.reserve(size);
  length_ = 0;
}

}  // namespace sketchy_service
