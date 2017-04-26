// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/nodes/shape_node.h"

namespace mozart {
namespace composer {

const ResourceTypeInfo ShapeNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kShapeNode, "ShapeNode"};

ShapeNode::ShapeNode(Session* session) : Node(session, ShapeNode::kTypeInfo) {}

void ShapeNode::SetMaterial(MaterialPtr material) {
  material_ = std::move(material);
}

void ShapeNode::SetShape(ShapePtr shape) {
  shape_ = std::move(shape);
}

}  // namespace composer
}  // namespace mozart
