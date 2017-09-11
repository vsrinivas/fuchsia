// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "sketchy/stroke.h"

#include "escher/shape/mesh_builder.h"
#include "sketchy/debug_print.h"
#include "sketchy/page.h"

namespace sketchy {

Stroke::Stroke(Page* page, StrokeId id)
    : page_(page), id_(id), finalized_(false) {}

void Stroke::Finalize() {
  bool was_finalized = finalized_.exchange(true);
  if (!was_finalized) {
    page_->FinalizeStroke(id_);
    FXL_DLOG(INFO) << "finalized " << *this;
  }
}

void Stroke::SetPath(StrokePath path) {
  FXL_DCHECK(!finalized_);
  path_ = std::move(path);
  length_ = 0.f;
  for (auto& seg : path_) {
    length_ += seg.length();
  }
  page_->dirty_strokes_.insert(this);
}

// TODO: Tessellate stroke on GPU.
void Stroke::Tessellate() {
  if (path_.empty()) {
    FXL_LOG(INFO) << "Stroke::Tessellate() PATH IS EMPTY";
    return;
  }

  std::vector<size_t> vertex_counts = page_->ComputeVertexCounts(path_);
  size_t total_vertex_count = 0;
  for (size_t count : vertex_counts) {
    FXL_DCHECK(count % 2 == 0);
    total_vertex_count += count;
  }
  vertex_count_ = total_vertex_count;

  auto builder = page_->escher_->NewMeshBuilder(
      escher::MeshSpec{escher::MeshAttribute::kPosition2D |
                       escher::MeshAttribute::kPositionOffset |
                       escher::MeshAttribute::kUV |
                       escher::MeshAttribute::kPerimeterPos},
      vertex_count_, vertex_count_ * 3);

  const float total_length_recip = 1.f / length_;

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
      const float cumulative_length = segment_start_length + (t * seg.length());

      struct StrokeVertex {
        vec2 pos;
        vec2 pos_offset;
        vec2 uv;
        float perimeter_pos;
      } vertex;

      vertex.pos_offset = point_and_normal.second * kStrokeWidth * 0.5f;
      vertex.pos = point_and_normal.first + vertex.pos_offset;
      vertex.perimeter_pos = cumulative_length * total_length_recip;
      vertex.uv = vec2(vertex.perimeter_pos, 1.f);
      builder->AddVertex(vertex);

      vertex.pos_offset *= -1.f;
      vertex.pos = point_and_normal.first + vertex.pos_offset;
      vertex.uv = vec2(vertex.perimeter_pos, 0.f);
      builder->AddVertex(vertex);
    }

    // Prepare for next segment.
    segment_start_length += seg.length();
  }

  // Generate indices.
  for (int i = 0; i < vertex_count_ - 2; i += 2) {
    builder->AddIndex(i).AddIndex(i + 1).AddIndex(i + 3);
    builder->AddIndex(i).AddIndex(i + 3).AddIndex(i + 2);
  }

  mesh_ = builder->Build();
}

}  // namespace sketchy
