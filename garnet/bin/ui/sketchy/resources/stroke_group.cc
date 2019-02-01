// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/resources/stroke_group.h"

#include "lib/ui/scenic/cpp/commands.h"

namespace {

std::pair<uint32_t, uint32_t> EstimateDeltaCounts(
    std::set<sketchy_service::StrokePtr> strokes) {
  uint32_t vertex_count = 0;
  uint32_t index_count = 0;
  for (auto it = strokes.begin(); it != strokes.end(); it++) {
    vertex_count += (*it)->vertex_count();
    index_count += (*it)->index_count();
  }
  return {vertex_count, index_count};
};

}  // namespace

namespace sketchy_service {

const ResourceTypeInfo StrokeGroup::kTypeInfo("StrokeGroup",
                                              ResourceType::kStrokeGroup,
                                              ResourceType::kResource);

StrokeGroup::StrokeGroup(scenic::Session* session)
    : shape_node_(session), mesh_(session), material_(session) {
  material_.SetColor(255, 0, 255, 255);
  shape_node_.SetMaterial(material_);
  shape_node_.SetShape(mesh_);
}

bool StrokeGroup::AddStroke(StrokePtr stroke) {
  if (strokes_to_add_.find(stroke) != strokes_to_add_.end()) {
    FXL_LOG(WARNING) << "Stroke " << stroke.get()
                     << " has already been added to this group.";
    return false;
  }
  if (!needs_re_tessellation_) {
    strokes_to_add_.insert(stroke);
  }
  strokes_.insert(stroke);
  return true;
}

bool StrokeGroup::RemoveStroke(StrokePtr stroke) {
  strokes_.erase(stroke);
  strokes_to_add_.erase(stroke);
  needs_re_tessellation_ = true;
  return true;
}

bool StrokeGroup::Clear() {
  strokes_.clear();
  strokes_to_add_.clear();
  needs_re_tessellation_ = true;
  return true;
}

void StrokeGroup::UpdateMesh(Frame* frame) {
  if (needs_re_tessellation_) {
    strokes_to_add_.clear();
    ReTessellateStrokes(frame);
  } else {
    MergeStrokes(frame);
  }
}

void StrokeGroup::MergeStrokes(Frame* frame) {
  if (strokes_to_add_.empty()) {
    FXL_LOG(WARNING) << "No stroke to add.";
    return;
  }
  auto pair = EstimateDeltaCounts(strokes_to_add_);
  mesh_buffer_.Prepare(frame, /* from_scratch= */ false, pair.first,
                       pair.second);
  while (!strokes_to_add_.empty()) {
    const auto& stroke = *strokes_to_add_.begin();
    strokes_to_add_.erase(stroke);
    stroke->TessellateAndMerge(frame, &mesh_buffer_);
  }
  mesh_buffer_.ProvideBuffersToScenicMesh(&mesh_);
}

void StrokeGroup::ReTessellateStrokes(Frame* frame) {
  auto pair = EstimateDeltaCounts(strokes_);
  mesh_buffer_.Prepare(frame, /* from_scratch= */ true, pair.first,
                       pair.second);
  for (auto it = strokes_.begin(); it != strokes_.end(); it++) {
    (*it)->TessellateAndMerge(frame, &mesh_buffer_);
  }
  mesh_buffer_.ProvideBuffersToScenicMesh(&mesh_);
  needs_re_tessellation_ = false;
}

}  // namespace sketchy_service
