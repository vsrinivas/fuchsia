// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "escher/escher.h"
#include "escher/shape/mesh_builder.h"
#include "escher/shape/mesh_spec.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

namespace {

static constexpr float kStrokeWidth = 60.f;  // pixels

std::vector<size_t> ComputeVertexCounts(
    const sketchy_service::StrokePath& path) {
  std::vector<size_t> counts;
  counts.reserve(path.size());
  for (auto& seg : path) {
    constexpr float kPixelsPerDivision = 4;
    size_t divisions = static_cast<size_t>(seg.length() / kPixelsPerDivision);
    // Each "division" of the stroke consists of two vertices, and we need at
    // least 2 divisions or else Stroke::Tessellate() might barf when computing
    // the "param_incr".
    counts.push_back(std::max(divisions * 2, 4UL));
  }
  return counts;
}

}  // namespace

namespace sketchy_service {

const ResourceTypeInfo Stroke::kTypeInfo("Stroke",
                                         ResourceType::kStroke,
                                         ResourceType::kResource);

Stroke::Stroke(escher::Escher* escher) : escher_(escher) {}

bool Stroke::SetPath(sketchy::StrokePathPtr path) {
  path_.clear();
  path_.reserve(path->segments.size());
  length_ = 0.f;
  for (auto& seg : path->segments) {
    path_.push_back({sketchy::CubicBezier2f{{
        {seg->pt0->x, seg->pt0->y},
        {seg->pt1->x, seg->pt1->y},
        {seg->pt2->x, seg->pt2->y},
        {seg->pt3->x, seg->pt3->y}}}});
    length_ += path_.back().length();
  }
  return true;
}

// TODO(MZ-269): The scenic mesh API takes position, uv, normal in order. For
// now only the position is used. The code that are commented out will be useful
// when we support wobble.
void Stroke::TessellateAndMerge(escher::impl::CommandBuffer* command,
                                escher::BufferFactory* buffer_factory,
                                StrokeGroup* stroke_group) {
  if (path_.empty()) {
    FXL_LOG(INFO) << "Stroke::Tessellate() PATH IS EMPTY";
    return;
  }

  std::vector<size_t> vertex_counts = ComputeVertexCounts(path_);
  size_t total_vertex_count = 0;
  for (size_t count : vertex_counts) {
    FXL_DCHECK(count % 2 == 0);
    total_vertex_count += count;
  }
  int vertex_count = total_vertex_count;
  int index_count = vertex_count * 3;

  auto builder = escher_->NewMeshBuilder(
      escher::MeshSpec{escher::MeshAttribute::kPosition2D |
                       escher::MeshAttribute::kPositionOffset
//                       escher::MeshAttribute::kUV |
//                       escher::MeshAttribute::kPerimeterPos
                       },
      vertex_count, index_count);

//  const float total_length_recip = 1.f / length_;

  // Use CPU to generate vertices for each Path segment.
  float segment_start_length = 0.f;
  for (size_t ii = 0; ii < path_.size(); ++ii) {
    auto& seg = path_[ii];
    auto& bez = seg.curve();
    auto& reparam = seg.arc_length_parameterization();

    const int seg_vert_count = vertex_counts[ii];

    // On all segments but the last, we don't want the Bezier parameter to
    // reach 1.0, because this would evaluate to the same thing as a parameter
    // of 0.0 on the next segment.
    const float param_incr = (ii == path_.size() - 1)
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
//      const float cumulative_length = segment_start_length + (t * seg.length());

      struct StrokeVertex {
        glm::vec2 pos;
        // TODO(MZ-269): It's supposed to be normal here, but ok now, as it's
        // not used. It will be helpful later when we support wobble.
        glm::vec2 pos_offset;
//        glm::vec2 uv;
//        float perimeter_pos;
      } vertex;

      vertex.pos_offset = point_and_normal.second * kStrokeWidth * 0.5f;
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
    segment_start_length += seg.length();
  }

  // Generate indices.
  for (int i = 0; i < vertex_count - 2; i += 2) {
    uint32_t j = i + stroke_group->num_vertices_;
    builder->AddIndex(j).AddIndex(j + 1).AddIndex(j + 3);
    builder->AddIndex(j).AddIndex(j + 3).AddIndex(j + 2);
  }

  auto mesh = builder->Build();

  // Start merging.
  stroke_group->vertex_buffer_->Merge(
      command, buffer_factory, mesh->vertex_buffer());
  stroke_group->index_buffer_->Merge(
      command, buffer_factory, mesh->index_buffer());
  stroke_group->num_vertices_ += mesh->num_vertices();
  stroke_group->num_indices_ += mesh->num_indices();
  stroke_group->bounding_box_.Join(mesh->bounding_box());
}

}  // namespace sketchy_service
