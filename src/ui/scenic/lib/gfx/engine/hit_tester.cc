// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/shape_node.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/traversal.h"
#include "src/ui/scenic/lib/gfx/resources/nodes/view_node.h"
#include "src/ui/scenic/lib/gfx/resources/view.h"

namespace scenic_impl {
namespace gfx {

std::vector<Hit> HitTester::HitTest(Node* node, const escher::ray4& ray) {
  FXL_DCHECK(node);
  FXL_DCHECK(ray_info_ == nullptr);
  FXL_DCHECK(intersection_info_ == nullptr);
  hits_.clear();  // Reset to good state after std::move.

  // Trace the ray.
  RayInfo local_ray_info{ray, glm::mat4(1.f)};
  ray_info_ = &local_ray_info;

  // Get start intersection info with infinite bounds.
  Node::IntersectionInfo intersection_info;
  intersection_info_ = &intersection_info;
  AccumulateHitsInner(node);
  ray_info_ = nullptr;
  intersection_info_ = nullptr;

  // Sort by distance.
  std::sort(hits_.begin(), hits_.end(),
            [](const Hit& a, const Hit& b) { return a.distance < b.distance; });

  // Warn if there are objects at the same distance as that is a user error.
  const std::string warning_message = GetDistanceCollisionsWarning(hits_);
  if (!warning_message.empty()) {
    FXL_LOG(WARNING) << warning_message;
  }

  return std::move(hits_);
}

void HitTester::AccumulateHitsOuter(Node* node) {
  // Take a fast path for identity transformations.
  if (node->transform().IsIdentity()) {
    AccumulateHitsInner(node);
    return;
  }

  // Apply the node's transformation to derive a new local ray.
  auto inverse_transform = glm::inverse(static_cast<glm::mat4>(node->transform()));
  RayInfo* outer_ray_info = ray_info_;
  RayInfo local_ray_info{inverse_transform * outer_ray_info->ray,
                         inverse_transform * outer_ray_info->inverse_transform};

  escher::ray4 outer_ray = outer_ray_info->ray;
  escher::ray4 local_ray = local_ray_info.ray;

  ray_info_ = &local_ray_info;
  AccumulateHitsInner(node);
  ray_info_ = outer_ray_info;
}

void HitTester::AccumulateHitsInner(Node* node) {
  // Bail if hit testing is suppressed.
  if (node->hit_test_behavior() == ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)
    return;

  if (node->clip_to_self() && node->ClipsRay(ray_info_->ray))
    return;

  Node::IntersectionInfo* outer_intersection = intersection_info_;
  Node::IntersectionInfo intersection = node->GetIntersection(ray_info_->ray, *intersection_info_);
  intersection_info_ = &intersection;

  if (intersection.did_hit) {
    hits_.emplace_back(
        Hit{node, ray_info_->ray, ray_info_->inverse_transform, intersection.distance});
  }

  // Only test the descendants if the current node permits it.
  if (intersection.continue_with_children) {
    ForEachDirectDescendantFrontToBack(*node, [this](Node* node) { AccumulateHitsOuter(node); });
  }

  intersection_info_ = outer_intersection;
}

std::string GetDistanceCollisionsWarning(const std::vector<Hit>& hits) {
  std::stringstream warning_message;

  bool message_started = false;
  for (size_t i = 0; i < hits.size();) {
    // Compare distances of adjacent hits.
    size_t num_colliding_nodes = 1;
    while (i + num_colliding_nodes < hits.size() &&
           hits[i].distance == hits[i + num_colliding_nodes].distance) {
      ++num_colliding_nodes;
    }

    // Create warning message if there were any collisions.
    if (num_colliding_nodes > 1) {
      if (!message_started) {
        warning_message << "Input-hittable nodes with ids ";
        message_started = true;
      }

      warning_message << "[ ";
      for (size_t j = 0; j < num_colliding_nodes; ++j) {
        warning_message << hits[i + j].node->global_id() << " ";
      }
      warning_message << "] ";
    }

    // Skip past collisions.
    i += num_colliding_nodes;
  }

  if (message_started) {
    warning_message << "are at equal distance and overlapping. See "
                       "https://fuchsia.dev/fuchsia-src/the-book/ui/view_bounds#collisions";
  }

  return warning_message.str();
}

}  // namespace gfx
}  // namespace scenic_impl
