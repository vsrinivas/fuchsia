// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/stroke/stroke_path.h"

namespace sketchy_service {

StrokePath::StrokePath(::fuchsia::ui::sketchy::StrokePath path) {
  Reset(path.segments->size());
  for (const auto& seg : *path.segments) {
    ExtendWithCurve(CubicBezier2f{{{seg.pt0.x, seg.pt0.y},
                                   {seg.pt1.x, seg.pt1.y},
                                   {seg.pt2.x, seg.pt2.y},
                                   {seg.pt3.x, seg.pt3.y}}});
  }
}

void StrokePath::ExtendWithCurve(const CubicBezier2f& curve) {
  auto pair = curve.ArcLengthParameterization();
  control_points_.push_back(curve);
  re_params_.push_back(pair.first);
  segment_lengths_.push_back(pair.second);
  length_ += pair.second;
}

void StrokePath::ExtendWithPath(const StrokePath& path) {
  control_points_.insert(control_points_.end(), path.control_points_.begin(),
                         path.control_points_.end());
  re_params_.insert(re_params_.end(), path.re_params_.begin(),
                    path.re_params_.end());
  segment_lengths_.insert(segment_lengths_.end(), path.segment_lengths_.begin(),
                          path.segment_lengths_.end());
  length_ += path.length_;
}

void StrokePath::Reset(size_t segment_count) {
  control_points_.clear();
  re_params_.clear();
  segment_lengths_.clear();
  length_ = 0;
  if (segment_count > 0) {
    control_points_.reserve(segment_count);
    re_params_.reserve(segment_count);
    segment_lengths_.reserve(segment_count);
  }
}

}  // namespace sketchy_service
