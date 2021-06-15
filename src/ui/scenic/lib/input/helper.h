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

namespace scenic_impl::input {

// Clone |event| and set its coordinates to |coords|.
fuchsia::ui::input::PointerEvent ClonePointerWithCoords(
    const fuchsia::ui::input::PointerEvent& event, const glm::vec2& coords);

// Extracts the coordinates from |event|.
glm::vec2 PointerCoords(const fuchsia::ui::input::PointerEvent& event);

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

// Converts a glm::mat4 to an array of a mat3 in column major order by shaving off the third row
// and column. This is valid for 2D-in-3D transforms affecting the xy-plane (i.e. how 2D content is
// handled in GFX).
//      Mat4                Mat3                   array
// [  1  2  3  4 ]      [  1  2  4 ]
// [  5  6  7  8 ]  ->  [  5  6  8 ]  ->  [ 1 5 13 2 6 14 4 8 16 ]
// [  9 10 11 12 ]      [ 13 14 16 ]
// [ 13 14 15 16 ]
Mat3ColumnMajorArray Mat4ToMat3ColumnMajorArray(const glm::mat4& mat);

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
