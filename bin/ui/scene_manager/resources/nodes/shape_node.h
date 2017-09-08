// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include "garnet/bin/ui/scene_manager/resources/nodes/node.h"

#include "garnet/bin/ui/scene_manager/resources/material.h"
#include "garnet/bin/ui/scene_manager/resources/shapes/shape.h"

namespace scene_manager {

class ShapeNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ShapeNode(Session* session, scenic::ResourceId node_id);

  void SetMaterial(MaterialPtr material);
  void SetShape(ShapePtr shape);

  const MaterialPtr& material() const { return material_; }
  const ShapePtr& shape() const { return shape_; }

  void Accept(class ResourceVisitor* visitor) override;

  bool GetIntersection(const escher::ray4& ray,
                       float* out_distance) const override;

 private:
  MaterialPtr material_;
  ShapePtr shape_;
};

}  // namespace scene_manager
