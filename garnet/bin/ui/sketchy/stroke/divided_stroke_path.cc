// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/stroke/divided_stroke_path.h"

#include "lib/escher/util/trace_macros.h"
#include "lib/fxl/logging.h"

namespace sketchy_service {

DividedStrokePath::DividedStrokePath(float half_width, float pixel_per_division)
    : half_width_(half_width),
      pixel_per_division_(pixel_per_division),
      path_(std::make_unique<StrokePath>()) {}

void DividedStrokePath::SetPath(std::unique_ptr<StrokePath> path) {
  Reset(path->segment_count());
  path_ = std::move(path);
  UpdateGeometry(*path_);
}

void DividedStrokePath::Extend(const StrokePath& delta_path) {
  path_->ExtendWithPath(delta_path);
  UpdateGeometry(delta_path);
}

void DividedStrokePath::Reset(size_t segment_count) {
  path_->Reset(segment_count);
  bbox_ = escher::BoundingBox();
  vertex_count_ = 0;
  index_count_ = 0;
  division_count_ = 0;
  vertex_counts_.clear();
  division_counts_.clear();
  cumulative_division_counts_.clear();
  if (segment_count > 0) {
    vertex_counts_.reserve(segment_count);
    division_counts_.reserve(segment_count);
    cumulative_division_counts_.reserve(segment_count);
  }
}

std::vector<uint32_t> DividedStrokePath::ComputeCumulativeDivisionCounts(
    uint32_t base_division_count) const {
  auto cumulative_division_counts = cumulative_division_counts_;
  for (size_t i = 0; i < cumulative_division_counts.size(); ++i) {
    cumulative_division_counts[i] += base_division_count;
  }
  return cumulative_division_counts;
}

std::vector<uint32_t> DividedStrokePath::PrepareDivisionSegmentIndices(
    const DividedStrokePath& trailing_path) {
  TRACE_DURATION(
      "gfx",
      "sketchy_service::DividedStrokePath::PrepareDivisionSegmentIndices");
  std::vector<uint32_t> division_segment_indices(division_count_ +
                                                 trailing_path.division_count_);
  for (uint32_t i = 0; i < division_counts_.size(); ++i) {
    auto begin =
        division_segment_indices.begin() + cumulative_division_counts_[i];
    auto end = begin + division_counts_[i];
    std::fill(begin, end, i);
  }
  for (uint32_t i = 0; i < trailing_path.division_counts_.size(); ++i) {
    auto begin = division_segment_indices.begin() + division_count_ +
                 trailing_path.cumulative_division_counts_[i];
    auto end = begin + trailing_path.division_counts_[i];
    std::fill(begin, end, division_counts_.size() + i);
  }
  return division_segment_indices;
}

void DividedStrokePath::UpdateGeometry(const StrokePath& delta_path) {
  for (const auto& seg : delta_path.control_points()) {
    glm::vec3 bmin = {
        std::min({seg.pts[0].x, seg.pts[1].x, seg.pts[2].x, seg.pts[3].x}),
        std::min({seg.pts[0].y, seg.pts[1].y, seg.pts[2].y, seg.pts[3].y}), 0};
    glm::vec3 bmax = {
        std::max({seg.pts[0].x, seg.pts[1].x, seg.pts[2].x, seg.pts[3].x}),
        std::max({seg.pts[0].y, seg.pts[1].y, seg.pts[2].y, seg.pts[3].y}), 0};
    bbox_.Join({bmin - half_width_, bmax + half_width_});
  }
  for (const auto& length : delta_path.segment_lengths()) {
    uint32_t division_count =
        std::max(1U, static_cast<uint32_t>(length / pixel_per_division_));
    division_counts_.push_back(division_count);
    cumulative_division_counts_.push_back(division_count_);
    division_count_ += division_count;
    uint32_t vertex_count = division_count * 2;
    vertex_counts_.push_back(vertex_count);
    vertex_count_ += vertex_count;
  }
  index_count_ = vertex_count_ * 3;
}

}  // namespace sketchy_service
