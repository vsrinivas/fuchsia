// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "apps/mozart/src/scene/resources/nodes/tag_node.h"

namespace mozart {
namespace scene {

const ResourceTypeInfo TagNode::kTypeInfo = {
    ResourceType::kNode | ResourceType::kTagNode, "TagNode"};

TagNode::TagNode(Session* session, ResourceId node_id, uint32_t tag_value)
    : Node(session, node_id, TagNode::kTypeInfo), tag_value_(tag_value) {}

HitTestResults TagNode::HitTest(const escher::vec2& point) const {
  HitTestResults results;

  ApplyOnDescendants(std::bind(&TagNode::HitTestVisitNode, this,
                               std::placeholders::_1, std::ref(results),
                               std::cref(point)));

  return results;
}

bool TagNode::HitTestVisitNode(const Node& child_node,
                               HitTestResults& results,
                               const escher::vec2& point) const {
  // Convert the point into the coordinate space of the child node.
  escher::vec2 child_point = child_node.ConvertPointFromNode(point, *this);

  if (child_node.type_flags() & ResourceType::kTagNode) {
    // If the child node is itself a tag node, we will initiate another
    // `HitTest` initiated at that tag node.
    for (auto& result :
         static_cast<const TagNode&>(child_node).HitTest(child_point)) {
      results.emplace_back(std::move(result));
    }
  } else {
    // If the descendant is a non-tag node, check if the point lies inside it.
    if (child_node.ContainsPoint(child_point)) {
      results.emplace_back(HitTestResult{resource_id(), point});
      // Stop traversal since we already know this tag node passed the hit test.
      return false;
    }
  }

  return true;
}

}  // namespace scene
}  // namespace mozart
