// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SHAPE_NODE_H_
#define SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SHAPE_NODE_H_

#include "src/ui/scenic/lib/gfx/resources/material.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/shapes/shape.h"

namespace scenic_impl {
namespace gfx {

class ShapeNode final : public Node {
 public:
  static const ResourceTypeInfo kTypeInfo;

  ShapeNode(Session* session, SessionId session_id, ResourceId node_id);

  void SetMaterial(MaterialPtr material);
  void SetShape(ShapePtr shape);

  const MaterialPtr& material() const { return material_; }
  const ShapePtr& shape() const { return shape_; }

  void Accept(class ResourceVisitor* visitor) override;

  IntersectionInfo GetIntersection(const escher::ray4& ray,
                                   const IntersectionInfo& parent_intersection) const override;

 private:
  MaterialPtr material_;
  ShapePtr shape_;
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_GFX_RESOURCES_NODES_SHAPE_NODE_H_
