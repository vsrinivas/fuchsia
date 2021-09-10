// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/helper.h"

#include <lib/syslog/cpp/macros.h>

#include "src/ui/scenic/lib/utils/math.h"

namespace scenic_impl::input {

using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using GfxPointerEvent = fuchsia::ui::input::PointerEvent;

trace_flow_id_t PointerTraceHACK(float fa, float fb) {
  uint32_t ia, ib;
  memcpy(&ia, &fa, sizeof(uint32_t));
  memcpy(&ib, &fb, sizeof(uint32_t));
  return (((uint64_t)ia) << 32) | ib;
}

std::pair<float, float> ReversePointerTraceHACK(trace_flow_id_t trace_id) {
  float fhigh, flow;
  const uint32_t ihigh = (uint32_t)(trace_id >> 32);
  const uint32_t ilow = (uint32_t)trace_id;
  memcpy(&fhigh, &ihigh, sizeof(uint32_t));
  memcpy(&flow, &ilow, sizeof(uint32_t));
  return {fhigh, flow};
}

Phase GfxPhaseToInternalPhase(PointerEventPhase phase) {
  switch (phase) {
    case PointerEventPhase::ADD:
      return Phase::kAdd;
    case PointerEventPhase::UP:
      return Phase::kUp;
    case PointerEventPhase::MOVE:
      return Phase::kChange;
    case PointerEventPhase::DOWN:
      return Phase::kDown;
    case PointerEventPhase::REMOVE:
      return Phase::kRemove;
    case PointerEventPhase::CANCEL:
      return Phase::kCancel;
    default:
      FX_CHECK(false) << "Should never be reached";
      return Phase::kInvalid;
  }
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

InternalTouchEvent GfxPointerEventToInternalEvent(const fuchsia::ui::input::PointerEvent& event,
                                                  zx_koid_t scene_koid, float screen_width,
                                                  float screen_height,
                                                  const glm::mat4& context_from_screen_transform) {
  InternalTouchEvent internal_event;
  internal_event.timestamp = event.event_time;
  internal_event.device_id = event.device_id;
  internal_event.pointer_id = event.pointer_id;
  // Define the viewport to match screen dimensions and location.
  internal_event.viewport.extents =
      Extents({{/*min*/ {0.f, 0.f}, /*max*/ {screen_width, screen_height}}});
  internal_event.viewport.context_from_viewport_transform = context_from_screen_transform;
  internal_event.position_in_viewport = {event.x, event.y};
  // Using scene_koid as both context and target, since it's guaranteed to be the root and thus
  // to deliver events to any client in the scene graph.
  internal_event.context = scene_koid;
  internal_event.target = scene_koid;
  internal_event.phase = GfxPhaseToInternalPhase(event.phase);
  internal_event.buttons = event.buttons;

  return internal_event;
}

GfxPointerEvent InternalPointerEventToGfxPointerEvent(const InternalTouchEvent& internal_event,
                                                      const glm::mat4& view_from_context_transform,
                                                      fuchsia::ui::input::PointerEventType type,
                                                      uint64_t trace_id) {
  GfxPointerEvent event;
  event.event_time = internal_event.timestamp;
  event.device_id = internal_event.device_id;
  event.pointer_id = internal_event.pointer_id;
  event.type = type;
  event.buttons = internal_event.buttons;

  // Convert to view-local coordinates.
  const glm::mat4 view_from_viewport_transform =
      view_from_context_transform * internal_event.viewport.context_from_viewport_transform;
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

Mat3ColumnMajorArray Mat4ToMat3ColumnMajorArray(const glm::mat4& mat) {
  return {mat[0][0], mat[0][1], mat[0][3], mat[1][0], mat[1][1],
          mat[1][3], mat[3][0], mat[3][1], mat[3][3]};
}

}  // namespace scenic_impl::input
