// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_
#define SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <lib/fit/function.h>

#include <map>

#include "lib/async/cpp/task.h"

namespace a11y {

// Struct for holding local and NDC coordinates for a particular pointer.
struct PointerLocation {
  // True if the pointer has not had an UP event since its last DOWN event.
  bool pointer_on_screen;

  // Coordinates of last event received for this pointer.
  ::fuchsia::math::PointF ndc_point;
  ::fuchsia::math::PointF local_point;
};

// Struct for holding context(Koid, location) about Gesture.
struct GestureContext {
  zx_koid_t view_ref_koid;
  uint32_t last_event_pointer_id;
  uint64_t last_event_time;
  fuchsia::ui::input::PointerEventPhase last_event_phase;
  std::map<uint32_t /*pointer_id*/, PointerLocation> starting_pointer_locations;
  std::map<uint32_t /*pointer id*/, PointerLocation> current_pointer_locations;

  ::fuchsia::math::PointF StartingCentroid(bool local) const;
  ::fuchsia::math::PointF CurrentCentroid(bool local) const;
};

// Max value by which pointer events can move(relative to the first point of contact), and still
// are valid for tap gestures, in NDC.
constexpr float kGestureMoveThreshold = 1.f / 16;

// Initializes a GestureContext given the first event.
// Returns false if required fields are missing, and true otherwise.
bool InitializeStartingGestureContext(
    const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
    GestureContext* gesture_context);

// Updates the location of a pointer given its most recent event.
// Returns false if required fields are missing or do not match starting info,
// and returns true otherwise.
bool UpdateGestureContext(const fuchsia::ui::input::accessibility::PointerEvent& pointer_event,
                          bool pointer_on_screen, GestureContext* gesture_context);

// Returns number of pointers currently in contact with screen.
uint32_t NumberOfFingersOnScreen(const GestureContext& gesture_context);

// Returns true if finger is in contact with screen and false otherwise.
bool FingerIsOnScreen(const GestureContext& gesture_context, uint32_t pointer_id);

// Resets GestureContext fields to default values.
void ResetGestureContext(GestureContext* gesture_context);

// Helper function to check if essential fields(like event time, device id, pointer id and ndc
// point) are present in the pointer event for the current gesture. It also makes sure that device
// id and pointer id has not changed for the gesture.
bool ValidatePointerEvent(const GestureContext& gesture_context,
                          const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

// Helper function to check if the provided pointer event is valid for current tap gesture being
// performed, by verifying the move threshold.
bool PointerEventIsValidTap(const GestureContext& gesture_start_context,
                            const fuchsia::ui::input::accessibility::PointerEvent& pointer_event);

// Returns the square of the distance between points a and b.
float SquareDistanceBetweenPoints(::fuchsia::math::PointF a, ::fuchsia::math::PointF b);
}  // namespace a11y

#endif  // SRC_UI_A11Y_LIB_GESTURE_MANAGER_GESTURE_UTIL_UTIL_H_
