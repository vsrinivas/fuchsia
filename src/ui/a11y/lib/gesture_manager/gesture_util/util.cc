// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>

#include "src/lib/syslog/cpp/logger.h"

namespace a11y {

bool InitGestureInfo(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
                     GestureInfo* gesture_start_info, GestureContext* gesture_context) {
  if (!pointer_event.has_event_time()) {
    return false;
  }
  gesture_start_info->gesture_start_time = pointer_event.event_time();

  if (!pointer_event.has_pointer_id()) {
    return false;
  }
  gesture_start_info->pointer_id = pointer_event.pointer_id();

  if (!pointer_event.has_device_id()) {
    return false;
  }
  gesture_start_info->device_id = pointer_event.device_id();

  if (!pointer_event.has_ndc_point()) {
    return false;
  }
  gesture_start_info->starting_ndc_position = pointer_event.ndc_point();

  if (pointer_event.has_local_point()) {
    gesture_start_info->starting_local_position = pointer_event.local_point();
  }

  if (pointer_event.has_viewref_koid()) {
    gesture_start_info->view_ref_koid = pointer_event.viewref_koid();
  }

  // Init GestureContext.
  gesture_context->view_ref_koid = gesture_start_info->view_ref_koid;
  if (gesture_start_info->starting_local_position) {
    gesture_context->local_point = gesture_start_info->starting_local_position;
  }
  return true;
}

void ResetGestureInfo(GestureInfo* gesture_info) {
  gesture_info->gesture_start_time = 0;

  gesture_info->starting_ndc_position.x = 0;
  gesture_info->starting_ndc_position.y = 0;

  // IMPORTANT! Do NOT set local coordinates to zero.
  //
  // The starting_local_position field uses a std::optional to accommodate the
  // case in which no starting position is present.
  gesture_info->starting_local_position.reset();

  gesture_info->device_id = 0;
  gesture_info->pointer_id = 0;
  gesture_info->view_ref_koid = ZX_KOID_INVALID;
}

void ResetGestureContext(GestureContext* gesture_context) {
  gesture_context->view_ref_koid = 0;

  // IMPORTANT! Do NOT set local coordinates to zero.
  //
  // The local_point field uses a std::optional to accommodate the
  // case in which no local position is present.
  gesture_context->local_point.reset();
}

bool ValidatePointerEvent(const GestureInfo gesture_start_info,
                          const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if pointer_event has all the required fields.
  if (!pointer_event.has_event_time() || !pointer_event.has_pointer_id() ||
      !pointer_event.has_device_id() || !pointer_event.has_ndc_point()) {
    FX_LOGS(ERROR) << "Pointer Event is missing required information.";
    return false;
  }

  // Check if pointer event information matches the gesture start information.
  if ((gesture_start_info.device_id != pointer_event.device_id()) ||
      (gesture_start_info.pointer_id != pointer_event.pointer_id())) {
    FX_LOGS(ERROR) << "Pointer event is not valid for current gesture.";
    return false;
  }
  return true;
}

}  // namespace a11y
