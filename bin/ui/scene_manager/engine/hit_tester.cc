// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/engine/hit_tester.h"

#include "garnet/bin/ui/scene_manager/resources/nodes/traversal.h"
#include "lib/ftl/logging.h"

namespace scene_manager {

HitTester::HitTester() {}

HitTester::~HitTester() = default;

std::vector<Hit> HitTester::HitTest(Node* node, const escher::ray4& ray) {
  FTL_DCHECK(node);
  FTL_DCHECK(session_ == nullptr);
  FTL_DCHECK(ray_info_ == nullptr);
  FTL_DCHECK(tag_info_ == nullptr);

  // Trace the ray.
  session_ = node->session();
  RayInfo local_ray_info{ray, escher::mat4(1.f)};
  ray_info_ = &local_ray_info;
  AccumulateHitsLocal(node);
  FTL_DCHECK(tag_info_ == nullptr);
  ray_info_ = nullptr;
  session_ = nullptr;

  // Sort by distance, preserving traversal order in case of ties.
  std::stable_sort(hits_.begin(), hits_.end(), [](const Hit& a, const Hit& b) {
    return a.distance < b.distance;
  });
  return std::move(hits_);
}

void HitTester::AccumulateHitsOuter(Node* node) {
  // Take a fast path for identity transformations.
  if (node->transform().IsIdentity()) {
    AccumulateHitsLocal(node);
    return;
  }

  // Apply the node's transformation to derive a new local ray.
  auto inverse_transform =
      glm::inverse(static_cast<escher::mat4>(node->transform()));
  RayInfo* outer_ray_info = ray_info_;
  RayInfo local_ray_info{inverse_transform * outer_ray_info->ray,
                         inverse_transform * outer_ray_info->inverse_transform};

  ray_info_ = &local_ray_info;
  AccumulateHitsLocal(node);
  ray_info_ = outer_ray_info;
}

void HitTester::AccumulateHitsLocal(Node* node) {
  // Bail if hit testing is suppressed.
  if (node->hit_test_behavior() == scenic::HitTestBehavior::kSuppress)
    return;

  // Take a fast path if the node does not contribute a tag to the hit test.
  if (!node->tag_value() || node->session() != session_) {
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
    hits_.emplace_back(Hit{node->tag_value(), ray_info_->inverse_transform,
                           local_tag_info.distance});
    if (outer_tag_info)
      outer_tag_info->ReportIntersection(local_tag_info.distance);
  }
}

void HitTester::AccumulateHitsInner(Node* node) {
  if (node->clip_to_self() && !IsRayWithinPartsInner(node, ray_info_->ray))
    return;

  float distance;
  if (tag_info_ && node->GetIntersection(ray_info_->ray, &distance)) {
    tag_info_->ReportIntersection(distance);
  }

  ForEachDirectDescendantFrontToBack(
      *node, [this](Node* node) { AccumulateHitsOuter(node); });
}

bool HitTester::IsRayWithinPartsInner(Node* node, const escher::ray4& ray) {
  return ForEachPartFrontToBackUntilTrue(*node, [&ray](Node* node) {
    return IsRayWithinClippedContentOuter(node, ray);
  });
}

bool HitTester::IsRayWithinClippedContentOuter(Node* node,
                                               const escher::ray4& ray) {
  if (node->transform().IsIdentity()) {
    return IsRayWithinClippedContentInner(node, ray);
  }

  auto inverse_transform =
      glm::inverse(static_cast<escher::mat4>(node->transform()));
  escher::ray4 local_ray = inverse_transform * ray;
  return IsRayWithinClippedContentInner(node, local_ray);
}

bool HitTester::IsRayWithinClippedContentInner(Node* node,
                                               const escher::ray4& ray) {
  float distance;
  if (node->GetIntersection(ray, &distance))
    return true;

  if (IsRayWithinPartsInner(node, ray))
    return true;

  if (node->clip_to_self())
    return false;

  return ForEachChildAndImportFrontToBackUntilTrue(*node, [&ray](Node* node) {
    return IsRayWithinClippedContentOuter(node, ray);
  });
}

}  // namespace scene_manager
