// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/sketchy/stroke/stroke_manager.h"
#include "lib/escher/profiling/timestamp_profiler.h"

namespace sketchy_service {

StrokeManager::StrokeManager(escher::EscherWeakPtr weak_escher)
    : stroke_tessellator_(std::move(weak_escher)) {}

bool StrokeManager::AddNewGroup(StrokeGroupPtr group) {
  group->SetNeedsReTessellation();
  dirty_stroke_groups_.insert(group);
  return true;
}

bool StrokeManager::AddStrokeToGroup(StrokePtr stroke, StrokeGroupPtr group) {
  if (stroke_to_group_map_.find(stroke) != stroke_to_group_map_.end()) {
    FXL_LOG(ERROR) << "Stroke has already been added to group!";
    return false;
  }
  stroke_to_group_map_.insert({stroke, group});
  dirty_stroke_groups_.insert(group);
  return group->AddStroke(std::move(stroke));
}

bool StrokeManager::RemoveStrokeFromGroup(StrokePtr stroke,
                                          StrokeGroupPtr group) {
  if (stroke_to_group_map_.find(stroke)->second != group) {
    FXL_LOG(ERROR) << "Stroke does not belong to group!";
    return false;
  }
  stroke_to_group_map_.erase(stroke);
  dirty_stroke_groups_.insert(group);
  return group->RemoveStroke(stroke);
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

bool StrokeManager::BeginStroke(StrokePtr stroke, glm::vec2 pt) {
  auto group_it = stroke_to_group_map_.find(stroke);
  if (group_it != stroke_to_group_map_.end()) {
    auto group = group_it->second;
    group->SetNeedsReTessellation();
    dirty_stroke_groups_.insert(group);
  }
  return stroke->Begin(pt);
}

bool StrokeManager::ExtendStroke(StrokePtr stroke,
                                 std::vector<glm::vec2> sampled_pts) {
  auto group_it = stroke_to_group_map_.find(stroke);
  if (group_it != stroke_to_group_map_.end()) {
    auto group = group_it->second;
    group->SetNeedsReTessellation();
    dirty_stroke_groups_.insert(group);
  }
  return stroke->Extend(std::move(sampled_pts));
}

bool StrokeManager::FinishStroke(StrokePtr stroke) {
  auto group_it = stroke_to_group_map_.find(stroke);
  if (group_it != stroke_to_group_map_.end()) {
    auto group = group_it->second;
    group->SetNeedsReTessellation();
    dirty_stroke_groups_.insert(group);
  }
  return stroke->Finish();
}

bool StrokeManager::ClearGroup(StrokeGroupPtr group) {
  dirty_stroke_groups_.insert(group);
  return group->Clear();
}

void StrokeManager::Update(Frame* frame) {
  while (!dirty_stroke_groups_.empty()) {
    const auto& stroke_group = *dirty_stroke_groups_.begin();
    dirty_stroke_groups_.erase(stroke_group);
    stroke_group->UpdateMesh(frame);
  }
}

}  // namespace sketchy_service
