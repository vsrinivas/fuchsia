// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
#define SRC_UI_SCENIC_LIB_INPUT_HELPER_H_

#include <fuchsia/ui/input/accessibility/cpp/fidl.h>
#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/pointerflow/cpp/fidl.h>

#include "src/ui/scenic/lib/gfx/gfx_system.h"

namespace scenic_impl {
namespace input {

// Clone |event| and set its coordinates to |coords|.
fuchsia::ui::input::PointerEvent ClonePointerWithCoords(
    const fuchsia::ui::input::PointerEvent& event, const glm::vec2& coords);

// Extracts the coordinates from |event|.
glm::vec2 PointerCoords(const fuchsia::ui::input::PointerEvent& event);

// Applies |transform| to |pointer|.
glm::vec2 TransformPointerCoords(const glm::vec2& pointer, const glm::mat4 transform);

// Finds (Vulkan) normalized device coordinates with respect to the (single) layer.
glm::vec2 NormalizePointerCoords(const glm::vec2& pointer, const gfx::LayerStackPtr& layer_stack);

// TODO(SCN-1278): Remove this.
// Turn two floats (high bits, low bits) into a 64-bit uint.
trace_flow_id_t PointerTraceHACK(float fa, float fb);

// TODO(SCN-1278): Remove this.
// Turn a 64-bit uint to two floats (high bits, low bits).
std::pair<float, float> ReversePointerTraceHACK(trace_flow_id_t n);

// Turns a pointerflow::Event into the corresponding PointerFlowEvents.
// Expects |event| to be a valid TOUCH pointer event.
// The mapping is directly translated, except for if the phase is ADD or REMOVE, in which case
// the event is duplicated and an extra phase is inserted.
// Mapping:
// event_time = timestamp
// pointer_id = pointer_id
// device_id = device_id
// x = position_x
// y = position_y
// type = fuchsia::ui::input::PointerEventType::TOUCH
//
// radius_minor and radius_major are set to the lower and upper bits of trace_flow_id if available
//
// Phase mapping:
// ADD -> ADD + DOWN
// CHANGE -> MOVE
// REMOVE -> UP + REMOVE
// CANCEL -> CANCEL
std::vector<fuchsia::ui::input::PointerEvent> PointerFlowEventToGfxPointerEvent(
    const fuchsia::ui::pointerflow::Event& event, uint32_t device_id);

}  // namespace input
}  // namespace scenic_impl

#endif  // SRC_UI_SCENIC_LIB_INPUT_HELPER_H_
