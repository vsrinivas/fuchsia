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

const ResourceTypeInfo Stroke::kTypeInfo("Stroke",
                                         ResourceType::kStroke,
                                         ResourceType::kResource);

Stroke::Stroke(StrokeTessellator* tessellator,
               escher::BufferFactory* buffer_factory)
    : tessellator_(tessellator),
      path_(kStrokeHalfWidth, kPixelsPerDivision),
      delta_path_(kStrokeHalfWidth, kPixelsPerDivision),
      stroke_info_buffer_(
          buffer_factory->NewBuffer(
              sizeof(StrokeInfo),
              vk::BufferUsageFlagBits::eUniformBuffer,
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
  path_.SetPath(std::move(path));
  return true;
}

bool Stroke::Begin(glm::vec2 pt) {
  if (fitter_) {
    FXL_LOG(ERROR) << "Client error: stroke fitting has already begun.";
    return false;
  }
  is_path_updated_ = true;
  path_.Reset();
  delta_path_.Reset();
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
  delta_path_.Extend(fitter_->path());
  fitter_->Reset();
  return true;
}

bool Stroke::Finish() {
  if (!fitter_) {
    FXL_LOG(ERROR) << "Client error: stroke fitting has not begun.";
    return false;
  }
  fitter_ = nullptr;
  return true;
}

void Stroke::TessellateAndMerge(
    Frame* frame, MeshBuffer* mesh_buffer) {
  if (path_.empty() && delta_path_.empty()) {
    return;
  }
  auto command = frame->command();
  auto buffer_factory = frame->shared_buffer_pool()->factory();
  auto profiler = frame->profiler();

  if (is_path_updated_) {
    if (!delta_path_.empty()) {
      control_points_buffer_.AppendData(
          command, buffer_factory, delta_path_.control_points_data(),
          delta_path_.control_points_size());
      re_params_buffer_.AppendData(
          command, buffer_factory, delta_path_.re_params_data(),
          delta_path_.re_params_size());
      division_counts_buffer_.AppendData(
          command, buffer_factory, delta_path_.division_counts_data(),
          delta_path_.division_counts_size());

      auto cumulative_division_count =
          delta_path_.ComputeCumulativeDivisionCounts(path_.division_count());
      cumulative_division_counts_buffer_.AppendData(
          command, buffer_factory,
          cumulative_division_count.data(),
          cumulative_division_count.size() * sizeof(uint32_t));

      path_.Extend(delta_path_.path());
      delta_path_.Reset();
    } else {
      control_points_buffer_.SetData(
          command, buffer_factory, path_.control_points_data(),
          path_.control_points_size());
      re_params_buffer_.SetData(
          command, buffer_factory, path_.re_params_data(),
          path_.re_params_size());
      division_counts_buffer_.SetData(
          command, buffer_factory, path_.division_counts_data(),
          path_.division_counts_size());
      cumulative_division_counts_buffer_.SetData(
          command, buffer_factory, path_.cumulative_division_counts_data(),
          path_.cumulative_division_counts_size());
    }

    auto division_segment_indices = path_.PrepareDivisionSegmentIndices();
    division_segment_index_buffer_.SetData(
        command, buffer_factory, division_segment_indices.data(),
        division_segment_indices.size() * sizeof(uint32_t));

    is_path_updated_ = false;
  }

  uint32_t base_vertex_index = mesh_buffer->vertex_count();
  auto pair = mesh_buffer->Preserve(
      frame, path_.vertex_count(), path_.index_count(), path_.bbox());
  auto vertex_buffer = std::move(pair.first);
  auto index_buffer = std::move(pair.second);

  StrokeInfo stroke_info = {
      .segment_count = static_cast<uint32_t>(path_.segment_count()),
      .half_width = kStrokeHalfWidth,
      .base_vertex_index = base_vertex_index,
      .pixels_per_division = kPixelsPerDivision,
      .division_count = path_.division_count(),
      .total_length = path_.length()
  };
  memcpy(stroke_info_buffer_->ptr(), &stroke_info, sizeof(StrokeInfo));

  tessellator_->Dispatch(
      stroke_info_buffer_, control_points_buffer_.get(),
      re_params_buffer_.get(), division_counts_buffer_.get(),
      cumulative_division_counts_buffer_.get(),
      division_segment_index_buffer_.get(),
      vertex_buffer, index_buffer,
      command, profiler, path_.division_count());

  // Dependency is pretty clear within the command buffer. The compute command
  // depends on the copy command for input. No further command depends on the
  // output of the compute command. Therefore, a barrier is not required here.
}

}  // namespace sketchy_service
