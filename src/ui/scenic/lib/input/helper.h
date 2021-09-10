// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <lib/trace/event.h>

#include "src/ui/scenic/lib/input/internal_pointer_event.h"

namespace scenic_impl::input {

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

// Turns a gfx pointer event into an InternalTouchEvent.
InternalTouchEvent GfxPointerEventToInternalEvent(const fuchsia::ui::input::PointerEvent& event,
                                                  zx_koid_t scene_koid, float screen_width,
                                                  float screen_height,
                                                  const glm::mat4& context_from_screen_transform);

// Turns an InternalTouchEvent into a gfx pointer event.
// Does not support HOVER events.
fuchsia::ui::input::PointerEvent InternalPointerEventToGfxPointerEvent(
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

}  // namespace scenic_impl::input

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
