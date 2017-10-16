// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/stroke_manager.h"

namespace sketchy_service {

StrokeManager::StrokeManager(escher::Escher* escher)
    : stroke_tessellator_(escher) {}

bool StrokeManager::AddStrokeToGroup(StrokePtr stroke, StrokeGroupPtr group) {
  if (stroke_to_group_map_.find(stroke) != stroke_to_group_map_.end()) {
    FXL_LOG(ERROR) << "Stroke has already been added to group!";
    return false;
  }
  stroke_to_group_map_.insert({stroke, group});
  dirty_stroke_groups_.insert(group);
  return group->AddStroke(std::move(stroke));
}

bool StrokeManager::SetStrokePath(StrokePtr stroke,
                                  std::unique_ptr<StrokePath> path) {
  if (!stroke->SetPath(std::move(path))) {
    return false;
  }
  if (stroke_to_group_map_.find(stroke) != stroke_to_group_map_.end()) {
    auto group = stroke_to_group_map_[stroke];
    group->SetNeedsReTessellation();
    dirty_stroke_groups_.insert(group);
  }
  return true;
}

void StrokeManager::Update(escher::impl::CommandBuffer* command,
                           escher::BufferFactory* buffer_factory) {
  while (!dirty_stroke_groups_.empty()) {
    const auto& stroke_group = *dirty_stroke_groups_.begin();
    dirty_stroke_groups_.erase(stroke_group);
    stroke_group->UpdateMesh(command, buffer_factory);
  }
}

}  // namespace sketchy_service
