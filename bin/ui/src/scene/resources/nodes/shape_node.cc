// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/resources/nodes/shape_node.h"

#include <utility>

namespace mozart {
namespace scene {

const ResourceTypeInfo ShapeNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kShapeNode, "ShapeNode"};

ShapeNode::ShapeNode(Session* session, ResourceId node_id)
    : Node(session, node_id, ShapeNode::kTypeInfo) {}

void ShapeNode::SetMaterial(MaterialPtr material) {
  material_ = std::move(material);
}

void ShapeNode::SetShape(ShapePtr shape) {
  shape_ = std::move(shape);
}

bool ShapeNode::ContainsPoint(const escher::vec2& point) const {
  if (!shape_) {
    return false;
  }

  return shape_->ContainsPoint(point);
}

}  // namespace scene
}  // namespace mozart
