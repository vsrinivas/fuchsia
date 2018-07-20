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
  origin.z = -1.f;
  Point3F direction;
  direction.z = 1.f;
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
fuchsia::math::PointF TransformPointerEvent(const Point3F& ray_origin,
                                            const Point3F& ray_direction,
                                            fuchsia::ui::gfx::Hit hit) {
  escher::mat4 hit_node_to_device_transform =
      scenic::gfx::Unwrap(hit.inverse_transform);
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

ManagerImpl::ManagerImpl(component::StartupContext* startup_context,
                         SemanticTree* semantic_tree)
    : startup_context_(startup_context), semantic_tree_(semantic_tree) {
  startup_context_->ConnectToEnvironmentService<
      fuchsia::ui::viewsv1::AccessibilityViewInspector>(
      a11y_view_inspector_.NewRequest());
  a11y_view_inspector_.set_error_handler([this]() {
    FXL_LOG(FATAL) << "Exiting due to view inspector connection error.";
  });
}

void ManagerImpl::AddBinding(
    fidl::InterfaceRequest<fuchsia::accessibility::Manager> request) {
  bindings_.AddBinding(this, std::move(request));
}

void ManagerImpl::GetHitAccessibilityNode(
    fuchsia::ui::viewsv1::ViewTreeToken token,
    fuchsia::ui::input::PointerEvent input,
    GetHitAccessibilityNodeCallback callback) {
  PointF point;
  point.x = input.x;
  point.y = input.y;
  std::pair<Point3F, Point3F> ray = DefaultRayForHitTestingScreenPoint(point);
  fuchsia::ui::viewsv1::AccessibilityViewInspector::PerformHitTestCallback
      view_callback =
          [this, callback = std::move(callback),
           point = std::move(point)](std::vector<fuchsia::ui::gfx::Hit> hits) {
            if (hits.empty()) {
              callback(-1, nullptr);
            } else {
              std::pair<Point3F, Point3F> ray =
                  DefaultRayForHitTestingScreenPoint(point);
              callback(hits.front().tag_value /*view_id*/,
                       semantic_tree_->GetHitAccessibilityNode(
                           hits.front().tag_value,
                           TransformPointerEvent(ray.first, ray.second,
                                                 hits.front() /*node*/)));
            }
          };
  a11y_view_inspector_->PerformHitTest(token, ray.first, ray.second,
                                       std::move(view_callback));
}

void ManagerImpl::SetAccessibilityFocus(int32_t view_id, int32_t node_id) {
  using ::fuchsia::accessibility::Action;
  if (a11y_focused_) {
    semantic_tree_->PerformAccessibilityAction(
        a11y_focused_view_id_, a11y_focused_node_id_,
        Action::LOSE_ACCESSIBILITY_FOCUS);
  }

  // TODO(SCN-853) Add way for a11y focus to be lost once the selected node is
  // deleted or hidden.
  a11y_focused_view_id_ = view_id;
  a11y_focused_node_id_ = node_id;
  a11y_focused_ = true;
  semantic_tree_->PerformAccessibilityAction(view_id, node_id,
                                             Action::GAIN_ACCESSIBILITY_FOCUS);
  fuchsia::accessibility::Node node;
  semantic_tree_->GetAccessibilityNode(view_id, node_id)->Clone(&node);

  // Notify front-ends that focus has changed. This is a bit of a hack,
  // because front-ends should ideally be signaling that focus has changed.
  // This might also be information in the tree not yet exposed yet.
  // TODO(SCN-854) Figure how the manager should be notified that actions
  // has been completed on the front-ends.
  BroadcastOnNodeAccessibilityAction(view_id, std::move(node),
                                     Action::GAIN_ACCESSIBILITY_FOCUS);
}

void ManagerImpl::PerformAccessibilityAction(
    fuchsia::accessibility::Action action) {
  if (a11y_focused_)
    semantic_tree_->PerformAccessibilityAction(a11y_focused_view_id_,
                                               a11y_focused_node_id_, action);
}

void ManagerImpl::BroadcastOnNodeAccessibilityAction(
    int32_t id, fuchsia::accessibility::Node node,
    fuchsia::accessibility::Action action) {
  for (auto& bind : bindings_.bindings()) {
    fuchsia::accessibility::Node node_copy;
    node.Clone(&node_copy);
    bind.get()->events().OnNodeAction(id, std::move(node_copy), action);
  }
}

}  // namespace a11y_manager
