// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"
#include "src/ui/scenic/lib/utils/math.h"
#include "src/ui/scenic/lib/view_tree/snapshot_types.h"

namespace scenic_impl::input {

// For converting between phase enum types.
// No support for HOVER phase.
fuchsia::ui::input::PointerEventPhase InternalPhaseToGfxPhase(Phase phase);

// Turns an InternalTouchEvent into a gfx pointer event.
// Does not support HOVER events.
fuchsia::ui::input::PointerEvent InternalTouchEventToGfxPointerEvent(
    const InternalTouchEvent& event, fuchsia::ui::input::PointerEventType type, uint64_t trace_id);

// Returns the 2D-transform from the viewport space of |event| to the destination view space as
// a mat3 in column-major array form.
// Prereq: |destination| must exist in the |snapshot|.
template <typename T>
ColumnMajorMat3Array GetDestinationFromViewportTransform(const T& event, zx_koid_t destination,
                                                         const view_tree::Snapshot& snapshot) {
  const std::optional<glm::mat4> destination_from_source_transform =
      snapshot.GetDestinationViewFromSourceViewTransform(/*source*/ event.context, destination);
  FX_DCHECK(destination_from_source_transform.has_value());
  const glm::mat4 destination_from_viewport_transform =
      destination_from_source_transform.value() * event.viewport.context_from_viewport_transform;
  return utils::Mat4ToColumnMajorMat3Array(destination_from_viewport_transform);
}

// Returns a copy of |event| with a new |receiver_from_viewport_transform| set on the viewport.
template <typename T>
T EventWithReceiverFromViewportTransform(const T& event, zx_koid_t receiver,
                                         const view_tree::Snapshot& snapshot) {
  T event_copy = event;
  event_copy.viewport.receiver_from_viewport_transform =
      GetDestinationFromViewportTransform(event, receiver, snapshot);
  return event_copy;
}

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
