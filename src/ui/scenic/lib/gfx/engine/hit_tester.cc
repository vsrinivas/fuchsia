// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"

#include <lib/syslog/cpp/macros.h>

#include <stack>

#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

namespace {

std::optional<ViewHit> CreateViewHit(const NodeHit& hit) {
  FX_DCHECK(hit.node);
  ViewPtr view = hit.node->FindOwningView();  // hit.node is a raw ptr, use and drop.
  if (!view) {
    return std::nullopt;
  }

  return ViewHit{
      .view_ref_koid = view->view_ref_koid(),
      .distance = hit.distance,
  };
}

// Checks if a node is hit by a ray. |local_ray| is the ray in the local space of the node.
Node::IntersectionInfo HitTestSingleNode(const Node* node, escher::ray4 local_ray,
                                         Node::IntersectionInfo parent_intersection,
                                         bool semantic_hit_test) {
  // Bail if hit testing is suppressed, the ray is clipped or if we're doing a semantic test and the
  // node is invisible to it.
  if (node->hit_test_behavior() == ::fuchsia::ui::gfx::HitTestBehavior::kSuppress ||
      (semantic_hit_test && !node->semantically_visible()) ||
      (node->clip_to_self() && node->ClipsRay(local_ray))) {
    return Node::IntersectionInfo{.did_hit = false, .continue_with_children = false};
  }

  return node->GetIntersection(local_ray, parent_intersection);
}

struct HitTestNode {
  // The node to perform the test on.
  const Node* node;
  // The intersection of the ray against the parent node.
  Node::IntersectionInfo parent_intersection;
};

}  // namespace

void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<NodeHit>* accumulator, bool semantic_hit_test) {
  FX_DCHECK(starting_node);
  FX_DCHECK(accumulator);

  // Hit testing scene graph iteratively by depth first traversal.
  std::stack<HitTestNode> stack;
  stack.push(HitTestNode{.node = starting_node, .parent_intersection = Node::IntersectionInfo()});
  while (!stack.empty()) {
    HitTestNode current_node = stack.top();
    stack.pop();

    // Get local reference frame.
    const glm::mat4 world_to_local_transform =
        glm::inverse(current_node.node->GetGlobalTransform());
    const escher::ray4 local_ray = world_to_local_transform * world_space_ray;

    // Perform hit test.
    const Node::IntersectionInfo local_intersection = HitTestSingleNode(
        current_node.node, local_ray, current_node.parent_intersection, semantic_hit_test);

    if (local_intersection.did_hit) {
      FX_VLOGS(2) << "\tHit: " << current_node.node->global_id();
      NodeHit hit{.node = current_node.node, .distance = local_intersection.distance};
      accumulator->Add(hit);
    }

    if (local_intersection.continue_with_children) {
      // Add all children to the stack.
      // Since each descendant is added to the stack and then processed in opposite order, the
      // actual traversal order here ends up being back-to-front.
      ForEachChildFrontToBack(*current_node.node, [&stack, &local_intersection](Node* child) {
        stack.push({.node = child, .parent_intersection = local_intersection});
      });
    }
  }
}

void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<ViewHit>* accumulator, bool semantic_hit_test) {
  MappingAccumulator<NodeHit, ViewHit> transforming_accumulator(
      accumulator, [](const NodeHit& hit) { return CreateViewHit(hit); });

  HitTest(starting_node, world_space_ray, &transforming_accumulator, semantic_hit_test);
  transforming_accumulator.EndLayer();
}

}  // namespace gfx
}  // namespace scenic_impl
