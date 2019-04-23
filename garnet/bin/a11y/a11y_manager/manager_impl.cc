// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/a11y/a11y_manager/manager_impl.h"

#include "garnet/lib/ui/gfx/util/unwrap.h"
#include "lib/escher/util/type_utils.h"

namespace a11y_manager {
using ::fuchsia::math::Point3F;
using ::fuchsia::math::PointF;

// Taken with minor modifications from the function in input_dispatcher.cc
// Returns a pair of points, {ray_origin, ray_direction}, in that order.
// The ray is constructed to point directly into the scene at the
// provided device coordinate.
std::pair<Point3F, Point3F> DefaultRayForHitTestingScreenPoint(
    const PointF& point) {
  Point3F origin;
  origin.x = point.x;
  origin.y = point.y;
  origin.z = 1.f;
  Point3F direction;
  direction.z = -1.f;
  return {origin, direction};
}

// Taken with minor modifications from the function in input_dispatcher.cc
// Transforms the raw input ray to the hit point in local coordinates of the
// view represented by a gfx::hit object.
//
// This transformation makes several assumptions:
//   * The ray must be the same as the one passed to |a11y_view_inspector_|'s
//     hit test, which determined the originally hit view.
//   * For MOVE and UP, which don't go through hit testing, the distance
//     is pinned to whatever distance the original hit occurred at. The origin
//     of the ray is the only thing that is shifted relative to the DOWN event.
//
// |ray_origin| is relative to the display's coordinate space.
// |ray_direction| is the direction of the ray in the device coordinate space.
// |hit| is the view hit representation returned by Scenic hit-testing.
// TODO(SCN-1124): This logic should move inside Scenic.
fuchsia::math::PointF TransformPointerEvent(const Point3F& ray_origin,
                                            const Point3F& ray_direction,
                                            fuchsia::ui::gfx::Hit hit) {
  escher::mat4 hit_node_to_device_transform =
      scenic_impl::gfx::Unwrap(hit.inverse_transform);
  escher::ray4 ray{{ray_origin.x, ray_origin.y, ray_origin.z, 1.f},
                   {ray_direction.x, ray_direction.y, ray_direction.z, 0.f}};
  escher::ray4 transformed_ray =
      glm::inverse(hit_node_to_device_transform) * ray;

  escher::vec4 hit_point = escher::homogenize(
      transformed_ray.origin + hit.distance * transformed_ray.direction);

  PointF point;
  point.x = hit_point[0];
  point.y = hit_point[1];
  return point;
}

void ManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::Manager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ManagerImpl::GetHitAccessibilityNode(
    fuchsia::ui::viewsv1::ViewTreeToken token,
    fuchsia::ui::input::PointerEvent input,
    GetHitAccessibilityNodeCallback callback) {
  // TODO(SCN-1124): wire hit tests through scenic a11y component
}

void ManagerImpl::SetAccessibilityFocus(int32_t view_id, int32_t node_id) {
  // TODO(MI4-1736): implement focus change with KOID-based semantic tree
}

void ManagerImpl::PerformAccessibilityAction(
    fuchsia::accessibility::semantics::Action action) {
  // TODO(MI4-1736): implement action with KOID-based semantic tree
}

void ManagerImpl::BroadcastOnNodeAccessibilityAction(
    int32_t id, fuchsia::accessibility::semantics::Node node,
    fuchsia::accessibility::semantics::Action action) {
  for (auto& bind : bindings_.bindings()) {
    fuchsia::accessibility::semantics::Node node_copy;
    node.Clone(&node_copy);
    bind.get()->events().OnNodeAction(id, std::move(node_copy), action);
  }
}

}  // namespace a11y_manager
