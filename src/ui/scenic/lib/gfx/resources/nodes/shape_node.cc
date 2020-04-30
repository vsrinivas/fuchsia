// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"

#include <utility>

namespace scenic_impl {
namespace gfx {

const ResourceTypeInfo ShapeNode::kTypeInfo = {ResourceType::kNode | ResourceType::kShapeNode,
                                               "ShapeNode"};

ShapeNode::ShapeNode(Session* session, SessionId session_id, ResourceId node_id)
    : Node(session, session_id, node_id, ShapeNode::kTypeInfo) {}

void ShapeNode::SetMaterial(MaterialPtr material) { material_ = std::move(material); }

void ShapeNode::SetShape(ShapePtr shape) { shape_ = std::move(shape); }

Node::IntersectionInfo ShapeNode::GetIntersection(
    const escher::ray4& ray, const IntersectionInfo& parent_intersection) const {
  FX_DCHECK(parent_intersection.continue_with_children);
  IntersectionInfo result;
  bool hit = shape_ && shape_->GetIntersection(ray, &result.distance);
  result.did_hit = hit && parent_intersection.interval.Contains(result.distance);
  result.interval = parent_intersection.interval;

  // Currently shape nodes cannot have children, but they may in the future and when
  // that happens we would like for children of shape nodes to be traversed, even if
  // the shape itself was not hit.
  result.continue_with_children = true;
  return result;
}

}  // namespace gfx
}  // namespace scenic_impl
