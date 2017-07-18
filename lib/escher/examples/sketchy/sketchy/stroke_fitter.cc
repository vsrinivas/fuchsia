// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sketchy/stroke_fitter.h"
#include "sketchy/debug_print.h"

namespace sketchy {

// TODO: make configurable.
static constexpr float kErrorThreshold = 10;

StrokeFitter::StrokeFitter(Page* page, StrokeId id)
    : page_(page),
      stroke_(page_->NewStroke(id)),
      stroke_id_(id),
      error_threshold_(kErrorThreshold) {}

StrokeFitter::~StrokeFitter() {
  FTL_DCHECK(finished_);
}

void StrokeFitter::StartStroke(vec2 pt) {
  points_.push_back(pt);
  params_.push_back(0.0);
}

void StrokeFitter::ContinueStroke(std::vector<vec2> sampled_points,
                                  std::vector<vec2> predicted_points) {
  FTL_DCHECK(page_->GetStroke(stroke_id_) != nullptr);

  bool changed = false;

  if (predicted_point_count_ > 0) {
    // Remove any points that were not actually sampled, only predicted.
    size_t trimmed_size = points_.size() - predicted_point_count_;
    points_.resize(trimmed_size);
    params_.resize(trimmed_size);
    predicted_point_count_ = 0;
    changed = true;
  }

  constexpr float kEpsilon = 0.000004;
  for (auto& pt : sampled_points) {
    float dist = distance(pt, points_.back());
    if (dist > kEpsilon) {
      points_.push_back(pt);
      params_.push_back(params_.back() + dist);
      changed = true;
    }
  }
  for (auto& pt : predicted_points) {
    float dist = distance(pt, points_.back());
    if (dist > kEpsilon) {
      points_.push_back(pt);
      params_.push_back(params_.back() + dist);
      ++predicted_point_count_;
      changed = true;
    }
  }

  if (!changed) {
    // There was no change since last time, so there is no need for refitting.
    return;
  }

  // Recursively compute a list of cubic Bezier segments.
  // TODO: don't recompute stable path segments near the beginning of the
  // stroke.
  size_t end_index = points_.size() - 1;
  vec2 left_tangent = points_[1] - points_[0];
  vec2 right_tangent = points_[end_index - 1] - points_[end_index];
  FitSampleRange(0, end_index, left_tangent, right_tangent);

  // TODO: remove... this is just basic sanity-check for Split() given that
  // I don't have unit tests running.
  StrokePath split_path;
  for (auto seg : path_) {
    auto split = seg.curve().Split(0.5);
    split_path.push_back(StrokeSegment(split.first));
    split_path.push_back(StrokeSegment(split.second));
  }
  path_ = std::move(split_path);

  // For each of the segments computed above, compute the total segment length
  // and a arc-length parameterization.  This parameterization is a 1-D cubic
  // Bezier such that an input parameter t in the range [0,1] results in a
  // new parameter t' (also in [0,1]) such that evaluating the original
  // curve-segment at t' returns the point on the segment where the cumulative
  // arc-length to that point is t * total_segment_length.
  FTL_DCHECK(!path_.empty());
  stroke_->SetPath(std::move(path_));
}

void StrokeFitter::FinishStroke() {
  FTL_DCHECK(page_->GetStroke(stroke_id_) != nullptr);
  stroke_->Finalize();
  points_.clear();
  params_.clear();
  finished_ = true;
}

void StrokeFitter::CancelStroke() {
  FTL_DCHECK(page_->GetStroke(stroke_id_) != nullptr);
  page_->DeleteStroke(stroke_id_);
  points_.clear();
  params_.clear();
  finished_ = true;
}

void StrokeFitter::FitSampleRange(int start_index,
                                  int end_index,
                                  vec2 left_tangent,
                                  vec2 right_tangent) {
  FTL_DCHECK(glm::length(left_tangent) > 0 && glm::length(right_tangent))
      << "  left: " << left_tangent << "  right: " << right_tangent;
  FTL_DCHECK(end_index > start_index);
  if (end_index - start_index == 1) {
    // Only two points... use a heuristic.
    // TODO: Double-check this heuristic (perhaps normalization needed?)
    // TODO: Perhaps this segment can be omitted entirely, e.g. by blending
    //       endpoints of the adjacent segments.
    CubicBezier2f line;
    line.pts[0] = points_[start_index];
    line.pts[3] = points_[end_index];
    line.pts[1] = line.pts[0] + (left_tangent * 0.25f);
    line.pts[2] = line.pts[3] + (right_tangent * 0.25f);
    FTL_DCHECK(!std::isnan(line.pts[0].x));
    FTL_DCHECK(!std::isnan(line.pts[0].y));
    FTL_DCHECK(!std::isnan(line.pts[1].x));
    FTL_DCHECK(!std::isnan(line.pts[1].y));
    FTL_DCHECK(!std::isnan(line.pts[2].x));
    FTL_DCHECK(!std::isnan(line.pts[2].y));
    FTL_DCHECK(!std::isnan(line.pts[3].x));
    FTL_DCHECK(!std::isnan(line.pts[3].y));
    path_.push_back(line);
    return;
  }

  // Normalize cumulative length between 0.0 and 1.0.
  float param_shift = -params_[start_index];
  float param_scale = 1.0 / (params_[end_index] + param_shift);

  CubicBezier2f bez =
      FitCubicBezier2f(&(points_[start_index]), end_index - start_index + 1,
                       &(params_[start_index]), param_shift, param_scale,
                       left_tangent, right_tangent);

  int split_index = (end_index + start_index + 1) / 2;
  float max_error = 0.0;
  for (int i = start_index; i <= end_index; ++i) {
    float t = (params_[i] + param_shift) * param_scale;
    vec2 diff = points_[i] - bez.Evaluate(t);
    float error = dot(diff, diff);
    if (error > max_error) {
      max_error = error;
      split_index = i;
    }
  }

  // The current fit is good enough... add it to the path and stop recursion.
  if (max_error < error_threshold_) {
    FTL_DCHECK(!std::isnan(bez.pts[0].x));
    FTL_DCHECK(!std::isnan(bez.pts[0].y));
    FTL_DCHECK(!std::isnan(bez.pts[1].x));
    FTL_DCHECK(!std::isnan(bez.pts[1].y));
    FTL_DCHECK(!std::isnan(bez.pts[2].x));
    FTL_DCHECK(!std::isnan(bez.pts[2].y));
    FTL_DCHECK(!std::isnan(bez.pts[3].x));
    FTL_DCHECK(!std::isnan(bez.pts[3].y));
    path_.push_back(bez);
    return;
  }

  // Error is too large... split into two ranges and fit each.
  FTL_DCHECK(split_index > start_index && split_index < end_index);
  // Compute the tangent on each side of the split point.
  // TODO: some filtering may be desirable here.
  vec2 right_middle_tangent = points_[split_index + 1] - points_[split_index];
  if (glm::length(right_middle_tangent) == 0.f) {
    // The two points on either side of the split point are identical: the
    // user's path doubled back upon itself.  Instead, compute the tangent using
    // the point at the split-index.
    right_middle_tangent = points_[split_index + 1] - points_[split_index];
  }
  vec2 left_middle_tangent = right_middle_tangent * -1.f;
  FitSampleRange(start_index, split_index, left_tangent, left_middle_tangent);
  FitSampleRange(split_index, end_index, right_middle_tangent, right_tangent);
}

}  // namespace sketchy
