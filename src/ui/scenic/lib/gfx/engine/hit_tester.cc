// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/gfx/engine/hit_tester.h"

#include <sstream>

#include "src/lib/fxl/logging.h"
#include "src/ui/lib/escher/geometry/types.h"
#include "src/ui/scenic/lib/gfx/engine/session.h"
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

}  // namespace

void HitTester::HitTest(Node* node, const escher::ray4& ray, HitAccumulator<NodeHit>* accumulator) {
  FXL_DCHECK(node);
  FXL_DCHECK(ray_ == nullptr);
  FXL_DCHECK(intersection_info_ == nullptr);
  FXL_DCHECK(accumulator);

  accumulator_ = accumulator;

  // Trace the ray.
  ray_ = &ray;

  // Get start intersection info with infinite bounds.
  Node::IntersectionInfo intersection_info;
  intersection_info_ = &intersection_info;
  AccumulateHitsOuter(node);
  ray_ = nullptr;
  intersection_info_ = nullptr;
  accumulator_ = nullptr;

  // Warn if there are objects at the same distance as that is a user error.
  LogDistanceCollisionWarning(collision_reporter_.Report());
  collision_reporter_.EndLayer();
}

void HitTester::AccumulateHitsOuter(Node* node) {
  // Take a fast path for identity transformations.
  if (node->transform().IsIdentity()) {
    AccumulateHitsInner(node);
    return;
  }

  // Invert the node's transform to derive a new local ray.
  const auto transform = glm::inverse(static_cast<glm::mat4>(node->transform()));
  const escher::ray4* outer_ray = ray_;
  const escher::ray4 local_ray = transform * *outer_ray;

  ray_ = &local_ray;
  AccumulateHitsInner(node);
  ray_ = outer_ray;
}

void HitTester::AccumulateHitsInner(Node* node) {
  // Bail if hit testing is suppressed.
  if (node->hit_test_behavior() == ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)
    return;

  if (node->clip_to_self() && node->ClipsRay(*ray_))
    return;

  Node::IntersectionInfo* outer_intersection = intersection_info_;
  Node::IntersectionInfo intersection = node->GetIntersection(*ray_, *intersection_info_);
  intersection_info_ = &intersection;

  if (intersection.did_hit) {
    FXL_VLOG(2) << "\tHit: " << node->global_id();
    accumulator_->Add({.node = node, .distance = intersection.distance});
  }

  // Only test the descendants if the current node permits it.
  if (intersection.continue_with_children) {
    ForEachDirectDescendantFrontToBack(*node, [this](Node* node) { AccumulateHitsOuter(node); });
  }

  intersection_info_ = outer_intersection;
}

}  // namespace gfx
}  // namespace scenic_impl
