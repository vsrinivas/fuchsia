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

namespace {

// Takes a ray in the coordinate system you are transforming to, the transform itself,
// and a point in the original coordinate system, and gets the distance of the transformed
// point to the ray origin. We assume that the point being passed in lies along the ray
// direction in the original transform space, so this function does not generalize to
// all possible points.
float GetTransformedDistance(const escher::ray4& local_ray, const glm::mat4& transform,
                             const glm::vec4& point) {
  return glm::length((transform * point) - local_ray.origin);
}

// This function transforms an intersection struct from one coordinate system to the other.
// Since the distances stored within a struct are the recorded distances between a ray
// origin and a node in a given  space, they need to be updated when the coordinate
// system changes.
Node::IntersectionInfo GetTransformedIntersection(const Node::IntersectionInfo& intersection,
                                                  const escher::ray4& outer_ray,
                                                  const escher::ray4& local_ray,
                                                  const glm::mat4& transform) {
  Node::IntersectionInfo local_intersection = intersection;
  // Get the coordinate points of the intersections based on the parameterized
  // distances.
  escher::Interval interval = intersection.interval;
  glm::vec4 min_point = outer_ray.At(interval.min());
  glm::vec4 max_point = outer_ray.At(interval.max());
  glm::vec4 dist_point = outer_ray.At(intersection.distance);

  // Transform the distances into the local coordinate system of the node and the
  // local ray, so that the math lines up.
  float local_min = GetTransformedDistance(local_ray, transform, min_point);
  float local_max = GetTransformedDistance(local_ray, transform, max_point);

  // Check for nan and inf in case the transformed distances got scaled beyond what
  // floating point values can handle.
  FXL_DCHECK(!std::isnan(local_min));
  FXL_DCHECK(!std::isnan(local_max));
  FXL_DCHECK(std::isfinite(local_max));
  FXL_DCHECK(local_min >= 0) << local_min;
  local_intersection.interval = escher::Interval(local_min, local_max);

  // Only transform the hit distance if there was an actual hit.
  if (intersection.did_hit) {
    float local_dst = GetTransformedDistance(local_ray, transform, dist_point);
    FXL_DCHECK(local_dst >= local_min) << local_dst << "," << local_min;
    FXL_DCHECK(local_dst <= local_max) << local_dst << "," << local_max;
    local_intersection.distance = local_dst;
  }
  return local_intersection;
}

// TODO(37712): Remove when parent propagation is removed and we no longer have false nodes.
bool IsHittableNode(const Node* node) {
  return node->IsKindOf<ViewNode>() || node->IsKindOf<ShapeNode>();
}

}  // namespace

std::vector<Hit> HitTester::HitTest(Node* node, const escher::ray4& ray) {
  FXL_DCHECK(node);
  FXL_DCHECK(ray_info_ == nullptr);
  FXL_DCHECK(tag_info_ == nullptr);
  FXL_DCHECK(intersection_info_ == nullptr);
  hits_.clear();  // Reset to good state after std::move.

  // Trace the ray.
  RayInfo local_ray_info{ray, glm::mat4(1.f)};
  ray_info_ = &local_ray_info;

  // Get start intersection info with infinite bounds.
  Node::IntersectionInfo intersection_info;
  intersection_info_ = &intersection_info;
  AccumulateHitsLocal(node);
  ray_info_ = nullptr;
  intersection_info_ = nullptr;

  FXL_DCHECK(tag_info_ == nullptr);

  // Sort by distance, preserving traversal order in case of ties.
  std::stable_sort(hits_.begin(), hits_.end(),
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
    AccumulateHitsLocal(node);
    return;
  }

  // Apply the node's transformation to derive a new local ray.
  auto inverse_transform = glm::inverse(static_cast<glm::mat4>(node->transform()));
  RayInfo* outer_ray_info = ray_info_;
  RayInfo local_ray_info{inverse_transform * outer_ray_info->ray,
                         inverse_transform * outer_ray_info->inverse_transform};

  escher::ray4 outer_ray = outer_ray_info->ray;
  escher::ray4 local_ray = local_ray_info.ray;

  // Make outer and local intersections.
  Node::IntersectionInfo* outer_intersection = intersection_info_;
  Node::IntersectionInfo local_intersection =
      GetTransformedIntersection(*outer_intersection, outer_ray, local_ray, inverse_transform);

  ray_info_ = &local_ray_info;
  intersection_info_ = &local_intersection;
  AccumulateHitsLocal(node);
  ray_info_ = outer_ray_info;
  intersection_info_ = outer_intersection;
}

void HitTester::AccumulateHitsLocal(Node* node) {
  // Bail if hit testing is suppressed.
  if (node->hit_test_behavior() == ::fuchsia::ui::gfx::HitTestBehavior::kSuppress)
    return;

  // Session-based hit testing may encounter nodes that don't participate.
  if (!should_participate(node)) {
    AccumulateHitsInner(node);
    return;
  }

  // The node is tagged by session which initiated the hit test.
  TagInfo* outer_tag_info = tag_info_;
  TagInfo local_tag_info{};

  tag_info_ = &local_tag_info;
  AccumulateHitsInner(node);
  tag_info_ = outer_tag_info;

  if (local_tag_info.is_hit()) {
    hits_.emplace_back(
        Hit{node, ray_info_->ray, ray_info_->inverse_transform, local_tag_info.distance});
    if (outer_tag_info)
      outer_tag_info->ReportIntersection(local_tag_info.distance);
  }
}

void HitTester::AccumulateHitsInner(Node* node) {
  if (node->clip_to_self() && node->ClipsRay(ray_info_->ray))
    return;

  Node::IntersectionInfo* outer_intersection = intersection_info_;
  Node::IntersectionInfo intersection = node->GetIntersection(ray_info_->ray, *intersection_info_);
  intersection_info_ = &intersection;

  if (intersection.did_hit && tag_info_) {
    tag_info_->ReportIntersection(intersection.distance);
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
    size_t num_colliding_nodes = IsHittableNode(hits[i].node) ? 1 : 0;
    size_t count = 1;
    while (i + count < hits.size() && hits[i].distance == hits[i + count].distance) {
      // Filter out false hits.
      // TODO(37712): Remove when we no longer have false hits.
      num_colliding_nodes += IsHittableNode(hits[i + count].node) ? 1 : 0;

      ++count;
    }

    // Create warning message if there were any collisions.
    if (num_colliding_nodes > 1) {
      if (!message_started) {
        warning_message << "Input-hittable nodes with ids ";
        message_started = true;
      }

      warning_message << "[ ";
      for (size_t j = 0; j < count; ++j) {
        if (IsHittableNode(hits[i + j].node)) {
          warning_message << hits[i + j].node->global_id() << " ";
        }
      }
      warning_message << "] ";
    }

    // Skip past collisions.
    i += count;
  }

  if (message_started) {
    warning_message << "are at equal distance and overlapping. See "
                       "https://fuchsia.dev/fuchsia-src/the-book/ui/view_bounds#collisions";
  }

  return warning_message.str();
}

}  // namespace gfx
}  // namespace scenic_impl
