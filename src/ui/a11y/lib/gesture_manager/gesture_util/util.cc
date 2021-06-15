// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/gesture_manager/gesture_util/util.h"

#include <lib/async/cpp/task.h>
#include <lib/async/default.h>
#include <lib/syslog/cpp/macros.h>

namespace a11y {

namespace {

::fuchsia::math::PointF Centroid(const std::vector<::fuchsia::math::PointF>& points) {
  ::fuchsia::math::PointF centroid;

  for (const auto& point : points) {
    centroid.x += point.x;
    centroid.y += point.y;
  }

  centroid.x /= static_cast<float>(points.size());
  centroid.y /= static_cast<float>(points.size());

  return centroid;
}

void UpdateLastEventInfo(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
                         GestureContext* gesture_context) {
  gesture_context->last_event_pointer_id = pointer_event.pointer_id();

  gesture_context->last_event_time =
      pointer_event.has_event_time() ? pointer_event.event_time() : 0;

  FX_DCHECK(pointer_event.has_phase());
  gesture_context->last_event_phase = pointer_event.phase();
}

}  // namespace

::fuchsia::math::PointF GestureContext::StartingCentroid(bool local) const {
  std::vector<::fuchsia::math::PointF> points;
  for (const auto& it : starting_pointer_locations) {
    points.push_back(local ? it.second.local_point : it.second.ndc_point);
  }

  return Centroid(points);
}

::fuchsia::math::PointF GestureContext::CurrentCentroid(bool local) const {
  std::vector<::fuchsia::math::PointF> points;
  for (const auto& it : current_pointer_locations) {
    points.push_back(local ? it.second.local_point : it.second.ndc_point);
  }

  return Centroid(points);
}

bool InitializeStartingGestureContext(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
    GestureContext* gesture_context) {
  uint32_t pointer_id;
  if (!pointer_event.has_pointer_id()) {
    return false;
  }
  pointer_id = pointer_event.pointer_id();

  if (!pointer_event.has_viewref_koid()) {
    return false;
  }
  gesture_context->view_ref_koid = pointer_event.viewref_koid();

  PointerLocation location;
  location.pointer_on_screen = true;

  if (pointer_event.has_ndc_point()) {
    location.ndc_point = pointer_event.ndc_point();
  }

  if (pointer_event.has_local_point()) {
    location.local_point = pointer_event.local_point();
  }

  gesture_context->starting_pointer_locations[pointer_id] =
      gesture_context->current_pointer_locations[pointer_id] = location;

  UpdateLastEventInfo(pointer_event, gesture_context);

  return true;
}

bool UpdateGestureContext(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
                          bool pointer_on_screen, GestureContext* gesture_context) {
  uint32_t pointer_id;
  if (!pointer_event.has_pointer_id()) {
    return false;
  }
  pointer_id = pointer_event.pointer_id();

  if (pointer_event.has_ndc_point()) {
    gesture_context->current_pointer_locations[pointer_id].ndc_point = pointer_event.ndc_point();
  }

  if (pointer_event.has_local_point()) {
    gesture_context->current_pointer_locations[pointer_id].local_point =
        pointer_event.local_point();
  }

  gesture_context->current_pointer_locations[pointer_id].pointer_on_screen = pointer_on_screen;

  UpdateLastEventInfo(pointer_event, gesture_context);

  return true;
}

uint32_t NumberOfFingersOnScreen(const GestureContext& gesture_context) {
  uint32_t num_fingers = 0;
  for (const auto& it : gesture_context.current_pointer_locations) {
    if (it.second.pointer_on_screen) {
      num_fingers++;
    }
  }

  return num_fingers;
}

bool FingerIsOnScreen(const GestureContext& gesture_context, uint32_t pointer_id) {
  if (!gesture_context.current_pointer_locations.count(pointer_id)) {
    return false;
  }

  return gesture_context.current_pointer_locations.at(pointer_id).pointer_on_screen;
}

void ResetGestureContext(GestureContext* gesture_context) {
  gesture_context->view_ref_koid = ZX_KOID_INVALID;
  gesture_context->starting_pointer_locations.clear();
  gesture_context->current_pointer_locations.clear();
}

bool ValidatePointerEvent(const GestureContext& gesture_context,
                          const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  // Check if pointer_event has all the required fields.
  if (!pointer_event.has_event_time() || !pointer_event.has_pointer_id() ||
      !pointer_event.has_device_id() || !pointer_event.has_ndc_point()) {
    FX_LOGS(INFO) << "Pointer Event is missing required information.";
    return false;
  }

  return gesture_context.starting_pointer_locations.count(pointer_event.pointer_id());
}

bool PointerEventIsValidTap(const GestureContext& gesture_context,
                            const fuchsia::ui::input::accessibility::PointerEvent& pointer_event) {
  FX_DCHECK(pointer_event.has_pointer_id());
  if (!gesture_context.starting_pointer_locations.count(pointer_event.pointer_id())) {
    return false;
  }

  return SquareDistanceBetweenPoints(
             pointer_event.ndc_point(),
             gesture_context.starting_pointer_locations.at(pointer_event.pointer_id()).ndc_point) <=
         kGestureMoveThreshold * kGestureMoveThreshold;
}

float SquareDistanceBetweenPoints(::fuchsia::math::PointF a, ::fuchsia::math::PointF b) {
  auto dx = a.x - b.x;
  auto dy = a.y - b.y;

  return dx * dx + dy * dy;
}
}  // namespace a11y
