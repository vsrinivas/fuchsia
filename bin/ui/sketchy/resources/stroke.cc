// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke.h"

#include "garnet/bin/ui/sketchy/buffer/shared_buffer_pool.h"
#include "lib/escher/escher.h"

namespace {

struct StrokeInfo {
  uint32_t segment_count;
  float half_width;
  uint32_t base_vertex_index;
  float pixels_per_division;
  uint32_t division_count;
  float total_length;
};

constexpr float kStrokeHalfWidth = 30.f;  // pixels
constexpr float kPixelsPerDivision = 4;

}  // namespace

namespace sketchy_service {

const ResourceTypeInfo Stroke::kTypeInfo("Stroke", ResourceType::kStroke,
                                         ResourceType::kResource);

Stroke::Stroke(StrokeTessellator* tessellator,
               escher::BufferFactory* buffer_factory)
    : tessellator_(tessellator),
      stable_path_(kStrokeHalfWidth, kPixelsPerDivision),
      delta_stable_path_(kStrokeHalfWidth, kPixelsPerDivision),
      stroke_info_buffer_(buffer_factory->NewBuffer(
          sizeof(StrokeInfo), vk::BufferUsageFlagBits::eUniformBuffer,
          vk::MemoryPropertyFlagBits::eHostVisible |
              vk::MemoryPropertyFlagBits::eHostCoherent)),
      control_points_buffer_(buffer_factory),
      re_params_buffer_(buffer_factory),
      division_counts_buffer_(buffer_factory),
      cumulative_division_counts_buffer_(buffer_factory),
      division_segment_index_buffer_(buffer_factory) {}

bool Stroke::SetPath(std::unique_ptr<StrokePath> path) {
  if (fitter_) {
    FXL_LOG(ERROR) << "Client error: path cannot be set during fitting.";
    return false;
  }
  is_path_updated_ = true;
  stable_path_.SetPath(std::move(path));
  delta_stable_path_.Reset();
  return true;
}

bool Stroke::Begin(glm::vec2 pt) {
  if (fitter_) {
    FXL_LOG(ERROR) << "Client error: stroke fitting has already begun.";
    return false;
  }

  is_path_updated_ = true;
  stable_path_.Reset();
  delta_stable_path_.Reset();
  fitter_ = std::make_unique<StrokeFitter>(pt);
  return true;
}

bool Stroke::Extend(const std::vector<glm::vec2>& sampled_pts) {
  if (!fitter_) {
    FXL_LOG(ERROR) << "Client error: stroke fitting has not begun.";
    return false;
  }

  is_path_updated_ = true;
  fitter_->Extend(sampled_pts);
  return true;
}

bool Stroke::Finish() {
  if (!fitter_) {
    FXL_LOG(ERROR) << "Client error: stroke fitting has not begun.";
    return false;
  }

  StrokePath delta_path;
  (void)fitter_->FitAndPop(&delta_path);
  if (!delta_path.empty()) {
    is_path_updated_ = true;
    delta_stable_path_.Extend(delta_path);
  }
  fitter_ = nullptr;
  return true;
}

void Stroke::TessellateAndMerge(Frame* frame, MeshBuffer* mesh_buffer) {
  DividedStrokePath delta_unstable_path(kStrokeHalfWidth, kPixelsPerDivision);
  if (fitter_) {
    StrokePath delta_path;
    bool is_stable = fitter_->FitAndPop(&delta_path);
    if (is_stable) {
      delta_stable_path_.Extend(delta_path);
    } else {
      delta_unstable_path.Extend(delta_path);
    }
  }
  if (stable_path_.empty() && delta_stable_path_.empty() &&
      delta_unstable_path.empty()) {
    return;
  }

  auto command = frame->command();
  auto buffer_factory = frame->shared_buffer_pool()->factory();
  auto profiler = frame->profiler();

  if (is_path_updated_) {
    if (!delta_stable_path_.empty()) {
      AppendPathToBuffers(command, buffer_factory, delta_stable_path_,
                          /* is_stable= */ true);
      stable_path_.Extend(delta_stable_path_.path());
      delta_stable_path_.Reset();
    } else if (!delta_unstable_path.empty()) {
      AppendPathToBuffers(command, buffer_factory, delta_unstable_path,
                          /* is_stable= */ false);
    } else {
      control_points_buffer_.SetData(command, buffer_factory,
                                     stable_path_.control_points_data(),
                                     stable_path_.control_points_data_size());
      re_params_buffer_.SetData(command, buffer_factory,
                                stable_path_.re_params_data(),
                                stable_path_.re_params_data_size());
      division_counts_buffer_.SetData(command, buffer_factory,
                                      stable_path_.division_counts_data(),
                                      stable_path_.division_counts_data_size());
      cumulative_division_counts_buffer_.SetData(
          command, buffer_factory,
          stable_path_.cumulative_division_counts_data(),
          stable_path_.cumulative_division_counts_data_size());
    }

    auto division_segment_indices =
        stable_path_.PrepareDivisionSegmentIndices(delta_unstable_path);
    division_segment_index_buffer_.SetData(
        command, buffer_factory, division_segment_indices.data(),
        division_segment_indices.size() * sizeof(uint32_t));
  }

  uint32_t base_vertex_index = mesh_buffer->vertex_count();
  auto pair = mesh_buffer->Reserve(
      frame, stable_path_.vertex_count() + delta_unstable_path.vertex_count(),
      stable_path_.index_count() + delta_unstable_path.index_count(),
      escher::BoundingBox()
          .Join(stable_path_.bbox())
          .Join(delta_unstable_path.bbox()));
  const auto& vertex_range = pair.first;
  const auto& index_range = pair.second;

  StrokeInfo stroke_info = {
      .segment_count = static_cast<uint32_t>(
          stable_path_.segment_count() + delta_unstable_path.segment_count()),
      .half_width = kStrokeHalfWidth,
      .base_vertex_index = base_vertex_index,
      .pixels_per_division = kPixelsPerDivision,
      .division_count =
          stable_path_.division_count() + delta_unstable_path.division_count(),
      .total_length = stable_path_.length() + delta_unstable_path.length()};
  memcpy(stroke_info_buffer_->host_ptr(), &stroke_info, sizeof(StrokeInfo));

  tessellator_->Dispatch(
      stroke_info_buffer_, control_points_buffer_.get(),
      re_params_buffer_.get(), division_counts_buffer_.get(),
      cumulative_division_counts_buffer_.get(),
      division_segment_index_buffer_.get(),
      mesh_buffer->vertex_buffer()->escher_buffer(), vertex_range,
      mesh_buffer->index_buffer()->escher_buffer(), index_range, command,
      profiler,
      stable_path_.division_count() + delta_unstable_path.division_count(),
      is_path_updated_);
  is_path_updated_ = false;
}

void Stroke::AppendPathToBuffers(escher::impl::CommandBuffer* command,
                                 escher::BufferFactory* buffer_factory,
                                 const DividedStrokePath& path,
                                 bool is_stable) {
  control_points_buffer_.AppendData(command, buffer_factory,
                                    path.control_points_data(),
                                    path.control_points_data_size(), is_stable);
  re_params_buffer_.AppendData(command, buffer_factory, path.re_params_data(),
                               path.re_params_data_size(), is_stable);
  division_counts_buffer_.AppendData(
      command, buffer_factory, path.division_counts_data(),
      path.division_counts_data_size(), is_stable);

  auto cumulative_division_count =
      path.ComputeCumulativeDivisionCounts(stable_path_.division_count());
  cumulative_division_counts_buffer_.AppendData(
      command, buffer_factory, cumulative_division_count.data(),
      cumulative_division_count.size() * sizeof(uint32_t), is_stable);
}

}  // namespace sketchy_service
