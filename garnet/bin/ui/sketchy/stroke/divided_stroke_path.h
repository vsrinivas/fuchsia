// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_STROKE_DIVIDED_STROKE_PATH_H_
#define GARNET_BIN_UI_SKETCHY_STROKE_DIVIDED_STROKE_PATH_H_

#include <vector>
#include "garnet/bin/ui/sketchy/stroke/stroke_path.h"
#include "lib/escher/geometry/bounding_box.h"

namespace sketchy_service {

// Wraps around a StrokePath. Contains division and bounding box info that is
// sufficient to generate a mesh.
class DividedStrokePath final {
 public:
  DividedStrokePath(float half_width, float pixel_per_division);
  void SetPath(std::unique_ptr<StrokePath> path);
  void Extend(const StrokePath& delta_path);
  void Reset(size_t segment_count = 0);

  // Compute the |cumulative_division_counts_| with and offset, as this path
  // might be a portion of a longer path.
  std::vector<uint32_t> ComputeCumulativeDivisionCounts(
      uint32_t base_division_count) const;

  // Fore each division, fill its segment index in |division_segment_indices_|.
  // This is a workaround solution to avoid dynamic branching in shader. It
  // could be expensive if |path_| is very very long.
  std::vector<uint32_t> PrepareDivisionSegmentIndices(
      const DividedStrokePath& trailing_path);

  const StrokePath& path() const { return *path_; }
  bool empty() const { return vertex_count_ == 0; }
  float length() const { return path_->length(); }
  size_t segment_count() const { return path_->segment_count(); }
  uint32_t division_count() const { return division_count_; }
  uint32_t vertex_count() const { return vertex_count_; }
  uint32_t index_count() const { return index_count_; }
  const escher::BoundingBox& bbox() const { return bbox_; }
  const void* control_points_data() const {
    return path_->control_points().data();
  }
  size_t control_points_data_size() const {
    return path_->control_points().size() * sizeof(CubicBezier2f);
  }
  const void* re_params_data() const { return path_->re_params().data(); }
  size_t re_params_data_size() const {
    return path_->re_params().size() * sizeof(CubicBezier1f);
  }
  const void* division_counts_data() const { return division_counts_.data(); }
  size_t division_counts_data_size() const {
    return division_counts_.size() * sizeof(uint32_t);
  }
  const void* cumulative_division_counts_data() const {
    return cumulative_division_counts_.data();
  }
  size_t cumulative_division_counts_data_size() const {
    return cumulative_division_counts_.size() * sizeof(uint32_t);
  }

 private:
  void UpdateGeometry(const StrokePath& delta_path);

  float half_width_;
  float pixel_per_division_;

  std::unique_ptr<StrokePath> path_;
  escher::BoundingBox bbox_;
  uint32_t vertex_count_ = 0;
  uint32_t index_count_ = 0;
  uint32_t division_count_ = 0;
  std::vector<uint32_t> vertex_counts_;
  std::vector<uint32_t> division_counts_;
  // Accumulates the previous (self exclusive) division counts.
  std::vector<uint32_t> cumulative_division_counts_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_STROKE_DIVIDED_STROKE_PATH_H_
