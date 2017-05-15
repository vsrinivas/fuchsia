// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/composer/resources/resource_visitor.h"

#include "apps/mozart/src/composer/resources/link.h"
#include "apps/mozart/src/composer/resources/material.h"
#include "apps/mozart/src/composer/resources/nodes/entity_node.h"
#include "apps/mozart/src/composer/resources/nodes/node.h"
#include "apps/mozart/src/composer/resources/nodes/shape_node.h"
#include "apps/mozart/src/composer/resources/shapes/circle_shape.h"
#include "apps/mozart/src/composer/resources/shapes/shape.h"

namespace mozart {
namespace composer {

void EntityNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Node::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void ShapeNode::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void CircleShape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Shape::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Link::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

void Material::Accept(ResourceVisitor* visitor) {
  visitor->Visit(this);
}

}  // namespace composer
}  // namespace mozart
