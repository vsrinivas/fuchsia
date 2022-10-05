// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/screen_reader/inject_pointer_event_action.h"

#include <lib/syslog/cpp/macros.h>

#include <set>

#include "src/ui/a11y/lib/screen_reader/screen_reader_context.h"
#include "src/ui/a11y/lib/screen_reader/util/util.h"

namespace a11y {

InjectPointerEventAction::InjectPointerEventAction(ActionContext* action_context,
                                                   ScreenReaderContext* screen_reader_context)
    : ScreenReaderAction(action_context, screen_reader_context) {}

InjectPointerEventAction::~InjectPointerEventAction() = default;

void InjectPointerEventAction::Run(GestureContext gesture_context) {
  auto a11y_focus = screen_reader_context_->GetA11yFocusManager()->GetA11yFocus();
  if (!a11y_focus || a11y_focus->view_ref_koid == ZX_KOID_INVALID) {
    return;
  }

  FX_DCHECK(action_context_->semantics_source);
  FX_DCHECK(action_context_->injector_manager);

  // Get local coordinates of the center of the currently focused node's
  // bounding box, and transform them to client-view-root space.
  auto* node = action_context_->semantics_source->GetSemanticNode(a11y_focus->view_ref_koid,
                                                                  a11y_focus->node_id);
  if (!node) {
    FX_LOGS(ERROR) << "Failed to inject pointer event into view. GetSemanticNode("
                   << a11y_focus->view_ref_koid << ", " << a11y_focus->node_id
                   << ") returned nullptr";
    return;
  }

  auto node_to_root_transform = action_context_->semantics_source->GetNodeToRootTransform(
      a11y_focus->view_ref_koid, a11y_focus->node_id);
  FX_DCHECK(node_to_root_transform);

  auto node_bounding_box = node->location();
  auto node_bounding_box_center_x = (node_bounding_box.min.x + node_bounding_box.max.x) / 2.f;
  auto node_bounding_box_center_y = (node_bounding_box.min.y + node_bounding_box.max.y) / 2.f;
  ::fuchsia::ui::gfx::vec3 node_bounding_box_center_local = {node_bounding_box_center_x,
                                                             node_bounding_box_center_y, 0.f};

  auto node_bounding_box_center_root =
      node_to_root_transform->Apply(node_bounding_box_center_local);

  // Get current displacement from gesture start location in client-view-root space.
  auto start_point = gesture_context.StartingCentroid(/* use_local_coordinates = */ true);
  auto current_point = gesture_context.CurrentCentroid(/* use_local_coordinates = */ true);
  fuchsia::math::PointF displacement = {current_point.x - start_point.x,
                                        current_point.y - start_point.y};

  // Get point at which to inject pointer event in client-view-root space.
  ::fuchsia::ui::gfx::vec3 action_target_root = {node_bounding_box_center_root.x + displacement.x,
                                                 node_bounding_box_center_root.y + displacement.y,
                                                 0.f};

  // Construct the pointer event to inject.
  fuchsia::ui::input::PointerEvent pointer_event;
  pointer_event.event_time = gesture_context.last_event_time;
  pointer_event.device_id = 1u;
  pointer_event.pointer_id = gesture_context.last_event_pointer_id;
  pointer_event.type = fuchsia::ui::input::PointerEventType::TOUCH;
  pointer_event.phase = gesture_context.last_event_phase;
  pointer_event.x = action_target_root.x;
  pointer_event.y = action_target_root.y;
  fuchsia::ui::input::InputEvent input_event;
  input_event.set_pointer(std::move(pointer_event));

  bool injection_result = action_context_->injector_manager->InjectEventIntoView(
      input_event, a11y_focus->view_ref_koid);
  if (!injection_result) {
    FX_LOGS(WARNING) << "Failed to inject event into view: " << a11y_focus->view_ref_koid;
  }
}

}  // namespace a11y
