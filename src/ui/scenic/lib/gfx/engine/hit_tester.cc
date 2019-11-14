// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"

#include <sstream>
#include <stack>

#include "src/lib/fxl/logging.h"
#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"

namespace scenic_impl {
namespace gfx {

namespace {

void LogDistanceCollisionWarning(const std::vector<std::vector<GlobalId>>& collisions) {
  if (!collisions.empty()) {
    std::ostringstream warning_message("Input-hittable nodes with ids ");

    for (const std::vector<GlobalId>& ids : collisions) {
      warning_message << "[ ";
      for (const GlobalId& id : ids) {
        warning_message << id << " ";
      }
      warning_message << "] ";
    }
    warning_message << "are at equal distance and overlapping. See "
                       "https://fuchsia.dev/fuchsia-src/the-book/ui/view_bounds#collisions";

    FXL_LOG(WARNING) << warning_message.str();
  }
}

// Checks if a node is hit by a ray. |local_ray| is the ray in the local space of the node.
Node::IntersectionInfo HitTestSingleNode(const Node* node, escher::ray4 local_ray,
                                         Node::IntersectionInfo parent_intersection) {
  // Bail if hit testing is suppressed or if the ray is clipped.
  if (node->hit_test_behavior() == ::fuchsia::ui::gfx::HitTestBehavior::kSuppress ||
      (node->clip_to_self() && node->ClipsRay(local_ray))) {
    return Node::IntersectionInfo{.did_hit = false, .continue_with_children = false};
  }

  return node->GetIntersection(local_ray, parent_intersection);
}

struct HitTestNode {
  // The node to perform the test on.
  const Node* node;
  // The ray in the local space of the parent.
  escher::ray4 parent_ray;
  // The intersection of the ray against the parent node.
  Node::IntersectionInfo parent_intersection;
};

}  // namespace

void HitTest(Node* root, const escher::ray4& ray, HitAccumulator<NodeHit>* accumulator) {
  FXL_DCHECK(root);
  FXL_DCHECK(accumulator);

  CollisionAccumulator collision_reporter;

  // Hit testing scene graph iteratively by depth first traversal.
  std::stack<HitTestNode> stack;
  stack.push(HitTestNode{
      .node = root, .parent_ray = ray, .parent_intersection = Node::IntersectionInfo()});
  while (!stack.empty()) {
    HitTestNode current_node = stack.top();
    stack.pop();

    // Get local reference frame.
    const glm::mat4 inverse_transform =
        glm::inverse(static_cast<glm::mat4>(current_node.node->transform()));
    const escher::ray4 local_ray = inverse_transform * current_node.parent_ray;

    // Perform hit test.
    const Node::IntersectionInfo local_intersection =
        HitTestSingleNode(current_node.node, local_ray, current_node.parent_intersection);

    if (local_intersection.did_hit) {
      FXL_VLOG(2) << "\tHit: " << current_node.node->global_id();
      NodeHit hit{.node = current_node.node, .distance = local_intersection.distance};
      collision_reporter.Add(hit);
      accumulator->Add(hit);
    }

    if (local_intersection.continue_with_children) {
      // Add all children to the stack.
      // Since each descendant is added to the stack and then processed in opposite order, the
      // actual traversal order here ends up being back-to-front.
      ForEachDirectDescendantFrontToBack(
          *current_node.node, [&stack, &local_ray, &local_intersection](Node* node) {
            stack.push(
                {.node = node, .parent_ray = local_ray, .parent_intersection = local_intersection});
          });
    }
  }

  // Warn if there are objects at the same distance as that is a user error.
  LogDistanceCollisionWarning(collision_reporter.Report());
}

}  // namespace gfx
}  // namespace scenic_impl
