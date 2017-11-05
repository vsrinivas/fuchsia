// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/sketchy/resources/import_node.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "garnet/bin/ui/sketchy/resources/stroke_group.h"
#include "garnet/bin/ui/sketchy/resources/stroke_tessellator.h"

namespace sketchy_service {

// Manages strokes and stroke groups.
class StrokeManager {
 public:
  explicit StrokeManager(escher::Escher* escher);

  bool AddStrokeToGroup(StrokePtr stroke, StrokeGroupPtr group);
  bool SetStrokePath(StrokePtr stroke, std::unique_ptr<StrokePath> path);

  void Update(escher::impl::CommandBuffer* command,
              escher::TimestampProfilerPtr profiler,
              escher::BufferFactory* buffer_factory);

  StrokeTessellator* stroke_tessellator() { return &stroke_tessellator_; }

 private:
  std::map<StrokePtr, StrokeGroupPtr> stroke_to_group_map_;
  std::set<StrokeGroupPtr> dirty_stroke_groups_;
  // TODO(MZ-269): Only have a tessellator per app.
  StrokeTessellator stroke_tessellator_;
};

}  // namespace sketchy_service
