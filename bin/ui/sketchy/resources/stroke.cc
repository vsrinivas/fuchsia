// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "lib/escher/escher.h"
#include "lib/escher/impl/command_buffer.h"
#include "lib/escher/shape/mesh_builder.h"
#include "lib/escher/util/trace_macros.h"

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

Stroke::Stroke(escher::Escher* escher, StrokeTessellator* tessellator)
    : escher_(escher),
      tessellator_(tessellator) {}

bool Stroke::SetPath(std::unique_ptr<StrokePath> path) {
  path_ = std::move(path);
  bbox_ = escher::BoundingBox();
  for (const auto& seg : path_->control_points()) {
    glm::vec3 bmin = {
        std::min({seg.pts[0].x, seg.pts[1].x, seg.pts[2].x, seg.pts[3].x}),
        std::min({seg.pts[0].y, seg.pts[1].y, seg.pts[2].y, seg.pts[3].y}),
        0};
    glm::vec3 bmax = {
        std::max({seg.pts[0].x, seg.pts[1].x, seg.pts[2].x, seg.pts[3].x}),
        std::max({seg.pts[0].y, seg.pts[1].y, seg.pts[2].y, seg.pts[3].y}),
        0};
    bbox_.Join({bmin - kStrokeHalfWidth, bmax + kStrokeHalfWidth});
  }

  vertex_count_ = 0;
  vertex_counts_.clear();
  vertex_counts_.reserve(path_->segment_count());
  division_count_ = 0;
  division_counts_.clear();
  division_counts_.reserve(path_->segment_count());
  cumulative_division_counts_.clear();
  cumulative_division_counts_.reserve(path_->segment_count());
  for (const auto& length : path_->segment_lengths()) {
    uint32_t division_count = std::max(1U, static_cast<uint32_t>(
        length / kPixelsPerDivision));
    division_counts_.push_back(division_count);
    cumulative_division_counts_.push_back(division_count_);
    division_count_ += division_count;
    uint32_t vertex_count = division_count * 2;
    vertex_counts_.push_back(vertex_count);
    vertex_count_ += vertex_count;
  }
  index_count_ = vertex_count_ * 3;

  // Prepare after division counts are done.
  PrepareDivisionSegmentIndices();

  control_points_buffer_ = nullptr;
  re_params_buffer_ = nullptr;
  division_counts_buffer_ = nullptr;
  cumulative_division_counts_buffer_ = nullptr;
  return true;
}

void Stroke::PrepareDivisionSegmentIndices() {
  TRACE_DURATION(
      "gfx", "sketchy_service::Stroke::PrepareDivisionSegmentIndices");
  division_segment_indices_.resize(division_count_);
  for (uint32_t i = 0; i < division_counts_.size(); i++) {
    auto begin =
        division_segment_indices_.begin() + cumulative_division_counts_[i];
    auto end = begin + division_counts_[i];
    std::fill(begin, end, i);
  }
}

void Stroke::TessellateAndMergeWithGpu(Frame* frame, MeshBuffer* mesh_buffer) {
  if (path_->empty()) {
    FXL_LOG(INFO) << "Stroke::Tessellate() PATH IS EMPTY";
    return;
  }
  auto command = frame->command();
  auto buffer_factory = frame->buffer_factory();
  auto profiler = frame->profiler();

  uint32_t base_vertex_index = mesh_buffer->vertex_count();
  auto pair = mesh_buffer->Preserve(
      command, buffer_factory, vertex_count_, index_count_, bbox_);
  auto vertex_buffer = std::move(pair.first);
  auto index_buffer = std::move(pair.second);

  StrokeInfo stroke_info = {
      .segment_count = static_cast<uint32_t>(path_->segment_count()),
      .half_width = kStrokeHalfWidth,
      .base_vertex_index = base_vertex_index,
      .pixels_per_division = kPixelsPerDivision,
      .division_count = division_count_,
      .total_length = path_->length()
  };

  tessellator_->Dispatch(
      GetOrCreateUniformBuffer(
          command, buffer_factory, stroke_info_buffer_,
          &stroke_info, sizeof(StrokeInfo)),
      GetOrCreateStorageBuffer(
          command, buffer_factory, control_points_buffer_,
          path_->control_points().data(), path_->control_points_size()),
      GetOrCreateStorageBuffer(
          command, buffer_factory, re_params_buffer_,
          path_->re_params().data(), path_->re_params_size()),
      GetOrCreateStorageBuffer(
          command, buffer_factory, division_counts_buffer_,
          division_counts_.data(),
          division_counts_.size() * sizeof(uint32_t)),
      GetOrCreateStorageBuffer(
          command, buffer_factory, cumulative_division_counts_buffer_,
          cumulative_division_counts_.data(),
          cumulative_division_counts_.size() * sizeof(uint32_t)),
      GetOrCreateStorageBuffer(
          command, buffer_factory, division_segment_index_buffer_,
          division_segment_indices_.data(),
          division_segment_indices_.size() * sizeof(uint32_t)),
      vertex_buffer, index_buffer,
      command, profiler, division_count_);

  // Dependency is pretty clear within the command buffer. The compute command
  // depends on the copy command for input. No further command depends on the
  // output of the compute command. Therefore, a barrier is not required here.
}

escher::BufferPtr Stroke::GetOrCreateUniformBuffer(
    escher::impl::CommandBuffer* command,
    escher::BufferFactory* buffer_factory,
    escher::BufferPtr& buffer, const void* data, size_t size) {
  FXL_CHECK(!path_->empty());
  if (!buffer.get()) {
    buffer = buffer_factory->NewBuffer(
        size,
        vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    // TODO: Copy data every time it's called when we support dynamic data in
    // it, e.g. time. For now it's static per stroke path.
    memcpy(buffer->ptr(), data, size);
  }
  return buffer;
}

escher::BufferPtr Stroke::GetOrCreateStorageBuffer(
    escher::impl::CommandBuffer* command,
    escher::BufferFactory* buffer_factory,
    escher::BufferPtr& buffer, const void* data, size_t size) {
  FXL_CHECK(!path_->empty());
  if (!buffer.get()) {
    auto staging_buffer = buffer_factory->NewBuffer(
        size,
        vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent);
    memcpy(staging_buffer->ptr(), data, size);

    buffer = buffer_factory->NewBuffer(
        size,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eDeviceLocal);
    // Only need to copy buffer once for the same path.
    command->CopyBuffer(staging_buffer, buffer, {0, 0, size});
  }
  return buffer;
}

// TODO(MZ-269): The scenic mesh API takes position, uv, normal in order. For
// now only the position is used. The code that are commented out will be useful
// when we support wobble.
void Stroke::TessellateAndMergeWithCpu(Frame* frame, MeshBuffer* mesh_buffer) {
  TRACE_DURATION(
      "gfx", "sketchy_service::Stroke::TessellateAndMergeWithCpu");
  if (path_->empty()) {
    FXL_LOG(INFO) << "Stroke::Tessellate() PATH IS EMPTY";
    return;
  }
  auto command = frame->command();
  auto buffer_factory = frame->buffer_factory();

  auto builder = escher_->NewMeshBuilder(
      escher::MeshSpec{
          escher::MeshAttribute::kPosition2D |
          escher::MeshAttribute::kPositionOffset
          //                       escher::MeshAttribute::kUV |
          //                       escher::MeshAttribute::kPerimeterPos
      },
      vertex_count_, index_count_);

  //  const float total_length_recip = 1.f / length_;

  // Use CPU to generate vertices for each Path segment.
  float segment_start_length = 0.f;
  for (size_t ii = 0; ii < path_->segment_count(); ++ii) {
    auto& bez = path_->control_points()[ii];
    auto& reparam = path_->re_params()[ii];

    const int seg_vert_count = vertex_counts_[ii];

    // On all segments but the last, we don't want the Bezier parameter to
    // reach 1.0, because this would evaluate to the same thing as a parameter
    // of 0.0 on the next segment.
    const float param_incr = (ii == path_->segment_count() - 1)
                                 ? 1.0 / (seg_vert_count - 2)
                                 : 1.0 / seg_vert_count;

    for (int i = 0; i < seg_vert_count; i += 2) {
      // We increment index by 2 each loop iteration, so the last iteration will
      // have "index == kVertexCount - 2", and therefore a parameter value of
      // "i * incr == 1.0".
      const float t = i * param_incr;
      // Use arc-length reparameterization before evaluating the segment's
      // curve.
      auto point_and_normal = EvaluatePointAndNormal(bez, reparam.Evaluate(t));
      //      const float cumulative_length = segment_start_length + (t *
      //      seg.length());

      struct StrokeVertex {
        glm::vec2 pos;
        // TODO(MZ-269): It's supposed to be normal here, but ok now, as it's
        // not used. It will be helpful later when we support wobble.
        glm::vec2 pos_offset;
        //        glm::vec2 uv;
        //        float perimeter_pos;
      } vertex;

      vertex.pos_offset = point_and_normal.second * kStrokeHalfWidth;
      vertex.pos = point_and_normal.first + vertex.pos_offset;
      //      vertex.perimeter_pos = cumulative_length * total_length_recip;
      //      vertex.uv = glm::vec2(vertex.perimeter_pos, 1.f);
      builder->AddVertex(vertex);

      vertex.pos_offset *= -1.f;
      vertex.pos = point_and_normal.first + vertex.pos_offset;
      //      vertex.uv = glm::vec2(vertex.perimeter_pos, 0.f);
      builder->AddVertex(vertex);
    }

    // Prepare for next segment.
    segment_start_length += path_->segment_lengths()[ii];
  }

  // Generate indices.
  for (int i = 0; i < static_cast<int>(vertex_count_) - 2; i += 2) {
    uint32_t j = i + mesh_buffer->vertex_count_;
    builder->AddIndex(j).AddIndex(j + 1).AddIndex(j + 3);
    builder->AddIndex(j).AddIndex(j + 3).AddIndex(j + 2);
  }

  auto mesh = builder->Build();

  // Start merging.
  mesh_buffer->vertex_buffer_->Merge(
      command, buffer_factory, mesh->vertex_buffer());
  mesh_buffer->index_buffer_->Merge(
      command, buffer_factory, mesh->index_buffer());
  mesh_buffer->vertex_count_ += mesh->num_vertices();
  mesh_buffer->index_count_ += mesh->num_indices();
  mesh_buffer->bbox_.Join(mesh->bounding_box());
}

}  // namespace sketchy_service
