// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerinjector/cpp/fidl.h>

#include "src/ui/scenic/lib/gfx/gfx_system.h"
#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl {
namespace input {

// Clone |event| and set its coordinates to |coords|.
fuchsia::ui::input::PointerEvent ClonePointerWithCoords(
    const fuchsia::ui::input::PointerEvent& event, const glm::vec2& coords);

// Extracts the coordinates from |event|.
glm::vec2 PointerCoords(const fuchsia::ui::input::PointerEvent& event);

// Applies |transform| to |pointer|.
glm::vec2 TransformPointerCoords(const glm::vec2& pointer, const glm::mat4 transform);

// TODO(fxbug.dev/24476): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb);

// TODO(fxbug.dev/24476): Remove this.
// Turn a 64-bit uint to two floats (high bits, low bits).
std::pair<float, float> ReversePointerTraceHACK(trace_flow_id_t n);

// For converting between phase enum types.
// No support for HOVER phase.
fuchsia::ui::input::PointerEventPhase InternalPhaseToGfxPhase(Phase phase);
Phase GfxPhaseToInternalPhase(fuchsia::ui::input::PointerEventPhase phase);

// Turns a pointerinjector::Event into the corresponding InternalPointerEvents.
// Expects |event| to be a valid TOUCH pointer event.
// The mapping is directly translated, except for if the phase is ADD or REMOVE, in which case
// the event is duplicated and an extra phase is inserted.
//
// Phase mapping:
// ADD -> ADD + DOWN
// CHANGE -> CHANGE
// REMOVE -> UP + REMOVE
// CANCEL -> CANCEL
std::vector<InternalPointerEvent> PointerInjectorEventToInternalPointerEvent(
    const fuchsia::ui::pointerinjector::Event& event, uint32_t device_id, const Viewport& viewport,
    zx_koid_t context, zx_koid_t target);

// Turns a gfx pointer event into an InternalPointerEvent.
InternalPointerEvent GfxPointerEventToInternalEvent(const fuchsia::ui::input::PointerEvent& event,
                                                    zx_koid_t scene_koid, float screen_width,
                                                    float screen_height,
                                                    const glm::mat4& context_from_screen_transform);

// Turns an InternalPointerEvent into a gfx pointer event.
// Does not support HOVER events.
fuchsia::ui::input::PointerEvent InternalPointerEventToGfxPointerEvent(
    const InternalPointerEvent& event, const glm::mat4& view_from_context_transform,
    fuchsia::ui::input::PointerEventType type, uint64_t trace_id);

glm::mat4 ColumnMajorMat3VectorToMat4(const std::array<float, 9>& matrix_array);

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
