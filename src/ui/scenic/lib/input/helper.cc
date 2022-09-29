// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/helper.h"

#include <lib/trace/event.h>

#include "src/ui/scenic/lib/utils/math.h"

namespace scenic_impl::input {

using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using GfxPointerEvent = fuchsia::ui::input::PointerEvent;

std::pair<float, float> ReversePointerTraceHACK(trace_flow_id_t trace_id) {
  float fhigh, flow;
  const uint32_t ihigh = (uint32_t)(trace_id >> 32);
  const uint32_t ilow = (uint32_t)trace_id;
  memcpy(&fhigh, &ihigh, sizeof(uint32_t));
  memcpy(&flow, &ilow, sizeof(uint32_t));
  return {fhigh, flow};
}

PointerEventPhase InternalPhaseToGfxPhase(Phase phase) {
  switch (phase) {
    case Phase::kAdd:
      return PointerEventPhase::ADD;
    case Phase::kUp:
      return PointerEventPhase::UP;
    case Phase::kChange:
      return PointerEventPhase::MOVE;
    case Phase::kDown:
      return PointerEventPhase::DOWN;
    case Phase::kRemove:
      return PointerEventPhase::REMOVE;
    case Phase::kCancel:
      return PointerEventPhase::CANCEL;
    case Phase::kInvalid:
      FX_CHECK(false) << "Should never be reached.";
      return static_cast<PointerEventPhase>(0);
  };
}

GfxPointerEvent InternalTouchEventToGfxPointerEvent(const InternalTouchEvent& internal_event,
                                                    fuchsia::ui::input::PointerEventType type,
                                                    uint64_t trace_id) {
  GfxPointerEvent event;
  event.event_time = internal_event.timestamp;
  event.device_id = internal_event.device_id;
  event.pointer_id = internal_event.pointer_id;
  event.type = type;
  event.buttons = internal_event.buttons;

  // Convert to view-local coordinates.
  FX_DCHECK(internal_event.viewport.receiver_from_viewport_transform.has_value());
  const glm::mat4 view_from_viewport_transform = utils::ColumnMajorMat3ArrayToMat4(
      internal_event.viewport.receiver_from_viewport_transform.value());
  const glm::vec2 local_position = utils::TransformPointerCoords(
      internal_event.position_in_viewport, view_from_viewport_transform);
  event.x = local_position.x;
  event.y = local_position.y;

  const auto [high, low] = ReversePointerTraceHACK(trace_id);
  event.radius_minor = low;   // Lower 32 bits.
  event.radius_major = high;  // Upper 32 bits.

  event.phase = InternalPhaseToGfxPhase(internal_event.phase);

  return event;
}

}  // namespace scenic_impl::input
