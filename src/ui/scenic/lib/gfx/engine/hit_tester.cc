// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"

#include <lib/syslog/cpp/macros.h>

#include <sstream>
#include <stack>

#include "src/ui/scenic/lib/gfx/engine/hit_accumulator.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/scene.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"

namespace scenic_impl {
namespace gfx {

namespace {

// TODO(45071): Re-enable when we no longer have known misbehaving clients.
// void LogDistanceCollisionWarning(const std::vector<std::vector<GlobalId>>& collisions) {
//   if (!collisions.empty()) {
//     std::ostringstream warning_message("Input-hittable nodes with ids ");

//     for (const std::vector<GlobalId>& ids : collisions) {
//       warning_message << "[ ";
//       for (const GlobalId& id : ids) {
//         warning_message << id << " ";
//       }
//       warning_message << "] ";
//     }
//     warning_message << "are at equal distance and overlapping. See "
//                        "https://fuchsia.dev/fuchsia-src/the-book/ui/view_bounds#collisions";

//     FX_LOGS(WARNING) << warning_message.str();
//   }
// }

std::optional<ViewHit> CreateViewHit(const NodeHit& hit,
                                     const glm::mat4& screen_to_world_transform) {
  FX_DCHECK(hit.node);
  ViewPtr view = hit.node->FindOwningView();  // hit.node is a raw ptr, use and drop.
  if (!view) {
    return std::nullopt;
  }

  FX_DCHECK(view->GetViewNode());

  const glm::mat4 world_to_local = glm::inverse(view->GetViewNode()->GetGlobalTransform());
  const glm::mat4 screen_to_local = world_to_local * screen_to_world_transform;
  return ViewHit{
      .view = view,
      .screen_to_view_transform = screen_to_local,
      .distance = hit.distance,
  };
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
  // The intersection of the ray against the parent node.
  Node::IntersectionInfo parent_intersection;
};

}  // namespace

void HitTest(Node* starting_node, const escher::ray4& world_space_ray,
             HitAccumulator<NodeHit>* accumulator) {
  FX_DCHECK(starting_node);
  FX_DCHECK(accumulator);

  CollisionAccumulator collision_reporter;

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
    const Node::IntersectionInfo local_intersection =
        HitTestSingleNode(current_node.node, local_ray, current_node.parent_intersection);

    if (local_intersection.did_hit) {
      FX_VLOGS(2) << "\tHit: " << current_node.node->global_id();
      NodeHit hit{.node = current_node.node, .distance = local_intersection.distance};
      collision_reporter.Add(hit);
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

  // TODO(45071): Re-enable when we no longer have known misbehaving clients.
  // Warn if there are objects at the same distance as that is a user error.
  // LogDistanceCollisionWarning(collision_reporter.Report());
}

void PerformGlobalHitTest(const LayerStackPtr& layer_stack, const glm::vec2& screen_space_coords,
                          HitAccumulator<ViewHit>* accumulator) {
  const escher::ray4 ray = CreateScreenPerpendicularRay(screen_space_coords);
  FX_VLOGS(1) << "HitTest: device point (" << ray.origin.x << ", " << ray.origin.y << ")";

  for (auto& layer : layer_stack->layers()) {
    const glm::mat4 screen_to_world_transform = layer->GetScreenToWorldSpaceTransform();

    MappingAccumulator<NodeHit, ViewHit> transforming_accumulator(
        accumulator, [transform = screen_to_world_transform](const NodeHit& hit) {
          return CreateViewHit(hit, transform);
        });

    const escher::ray4 camera_ray = screen_to_world_transform * ray;
    fxl::WeakPtr<Scene> scene = layer->scene();
    if (scene)
      HitTest(scene.get(), camera_ray, &transforming_accumulator);

    if (!accumulator->EndLayer()) {
      break;
    }
  }
}

escher::ray4 CreateScreenPerpendicularRay(glm::vec2 screen_space_coords) {
  // We set the elevation for the origin point, and Z value for the direction,
  // such that we start above the scene and point into the scene.
  //
  // During hit testing, we translate an arbitrary pointer's (x,y) Screen Space
  // coordinates to a View's (x', y') Local Space coordinates.
  return {
      // Origin as homogeneous point.
      .origin = {screen_space_coords.x, screen_space_coords.y, 0, 1},
      .direction = {0, 0, 1, 0},
  };
}

}  // namespace gfx
}  // namespace scenic_impl
