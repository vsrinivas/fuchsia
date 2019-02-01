// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_STROKE_STROKE_MANAGER_H_
#define GARNET_BIN_UI_SKETCHY_STROKE_STROKE_MANAGER_H_

#include "garnet/bin/ui/sketchy/frame.h"
#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/stroke/stroke_tessellator.h"

namespace sketchy_service {

// Manages strokes and stroke groups.
class StrokeManager {
 public:
  explicit StrokeManager(escher::EscherWeakPtr escher);

  bool AddNewGroup(StrokeGroupPtr group);
  bool AddStrokeToGroup(StrokePtr stroke, StrokeGroupPtr group);
  bool RemoveStrokeFromGroup(StrokePtr stroke, StrokeGroupPtr group);
  bool SetStrokePath(StrokePtr stroke, std::unique_ptr<StrokePath> path);

  // The group will be marked as need re-tessellation per following call. For
  // efficient rendering, client should keep the growing stroke in a temporary
  // group, and move them to a stable group once done.
  bool BeginStroke(StrokePtr stroke, glm::vec2 pt);
  // TODO(MZ-269): Also pass in predicted points.
  bool ExtendStroke(StrokePtr stroke, std::vector<glm::vec2> sampled_pts);
  bool FinishStroke(StrokePtr stroke);

  bool ClearGroup(StrokeGroupPtr group);

  void Update(Frame* frame);

  StrokeTessellator* stroke_tessellator() { return &stroke_tessellator_; }

 private:
  std::map<StrokePtr, StrokeGroupPtr> stroke_to_group_map_;
  std::set<StrokeGroupPtr> dirty_stroke_groups_;
  // TODO(MZ-269): Only have a tessellator per app.
  StrokeTessellator stroke_tessellator_;
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_STROKE_STROKE_MANAGER_H_
