// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_GROUP_H_
#define GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_GROUP_H_

#include <set>
#include "garnet/bin/ui/sketchy/buffer/mesh_buffer.h"
#include "garnet/bin/ui/sketchy/frame.h"
#include "garnet/bin/ui/sketchy/resources/resource.h"
#include "garnet/bin/ui/sketchy/resources/stroke.h"
#include "lib/escher/geometry/bounding_box.h"
#include "lib/ui/scenic/client/resources.h"
#include "lib/ui/scenic/client/session.h"

namespace sketchy_service {

class StrokeGroup;
using StrokeGroupPtr = fxl::RefPtr<StrokeGroup>;

class StrokeGroup final : public Resource {
 public:
  static const ResourceTypeInfo kTypeInfo;
  const ResourceTypeInfo& type_info() const override { return kTypeInfo; }

  explicit StrokeGroup(scenic::Session* session);

  // Record the stroke to add.
  bool AddStroke(StrokePtr stroke);
  // Remove a stroke from record.
  bool RemoveStroke(StrokePtr stroke);
  // Clear all strokes from record.
  bool Clear();

  void SetNeedsReTessellation() { needs_re_tessellation_ = true; }

  // Record the command to update the mesh.
  void UpdateMesh(Frame* frame);

  const scenic::ShapeNode& shape_node() const { return shape_node_; }

 private:
  // Record the command to merge the strokes to add.
  void MergeStrokes(Frame* frame);

  // Record the command to re-tessellate the strokes.
  void ReTessellateStrokes(Frame* frame);

  scenic::ShapeNode shape_node_;
  scenic::Mesh mesh_;
  scenic::Material material_;

  std::set<StrokePtr> strokes_to_add_;
  std::set<StrokePtr> strokes_;
  MeshBuffer mesh_buffer_;
  bool needs_re_tessellation_ = false;

  FXL_DISALLOW_COPY_AND_ASSIGN(StrokeGroup);
};

}  // namespace sketchy_service

#endif  // GARNET_BIN_UI_SKETCHY_RESOURCES_STROKE_GROUP_H_
