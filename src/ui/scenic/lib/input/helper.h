// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// For converting between phase enum types.
// No support for HOVER phase.
fuchsia::ui::input::PointerEventPhase InternalPhaseToGfxPhase(Phase phase);

// Turns an InternalTouchEvent into a gfx pointer event.
// Does not support HOVER events.
fuchsia::ui::input::PointerEvent InternalTouchEventToGfxPointerEvent(
    const InternalTouchEvent& event, const glm::mat4& view_from_context_transform,
    fuchsia::ui::input::PointerEventType type, uint64_t trace_id);

// Converts a glm::mat4 to an array of a mat3 in column major order by shaving off the third row
// and column. This is valid for 2D-in-3D transforms affecting the xy-plane (i.e. how 2D content is
// handled in GFX).
//      Mat4                Mat3                   array
// [  1  2  3  4 ]      [  1  2  4 ]
// [  5  6  7  8 ]  ->  [  5  6  8 ]  ->  [ 1 5 13 2 6 14 4 8 16 ]
// [  9 10 11 12 ]      [ 13 14 16 ]
// [ 13 14 15 16 ]
Mat3ColumnMajorArray Mat4ToMat3ColumnMajorArray(const glm::mat4& mat);

// Returns the 2D-transform from the viewport space of |event| to the destination view space as
// a mat3 in column-major array form.
// Prereq: |destination| must exist in the |snapshot|.
template <typename T>
Mat3ColumnMajorArray GetDestinationFromViewportTransform(const T& event, zx_koid_t destination,
                                                         const view_tree::Snapshot& snapshot) {
  const std::optional<glm::mat4> destination_from_source_transform =
      snapshot.GetDestinationViewFromSourceViewTransform(/*source*/ event.context, destination);
  FX_DCHECK(destination_from_source_transform.has_value());
  const glm::mat4 destination_from_viewport_transform =
      destination_from_source_transform.value() * event.viewport.context_from_viewport_transform;
  return Mat4ToMat3ColumnMajorArray(destination_from_viewport_transform);
}

// Returns a copy of |event| with a new |receiver_from_viewport_transform| set on the viewport.
template <typename T>
T EventWithReceiverFromViewportTransform(const T& event, zx_koid_t receiver,
                                         const view_tree::Snapshot& snapshot) {
  T event_copy = event;
  event_copy.viewport.receiver_from_viewport_transform =
      GetDestinationFromViewportTransform(event, event.target, snapshot);
  return event_copy;
}

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
