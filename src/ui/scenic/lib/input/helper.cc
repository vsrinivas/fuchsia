// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/lib/input/helper.h"

#include "src/ui/lib/escher/util/type_utils.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer.h"
#include "src/ui/scenic/lib/gfx/resources/compositor/layer_stack.h"

namespace scenic_impl {
namespace input {

using PointerEventPhase = fuchsia::ui::input::PointerEventPhase;
using GfxPointerEvent = fuchsia::ui::input::PointerEvent;
using InjectorEventPhase = fuchsia::ui::pointerinjector::EventPhase;

GfxPointerEvent ClonePointerWithCoords(const GfxPointerEvent& event, const glm::vec2& coords) {
  GfxPointerEvent clone;
  fidl::Clone(event, &clone);
  clone.x = coords.x;
  clone.y = coords.y;
  return clone;
}

glm::vec2 PointerCoords(const GfxPointerEvent& event) { return {event.x, event.y}; }

glm::vec2 TransformPointerCoords(const glm::vec2& pointer, const glm::mat4 transform) {
  const glm::vec4 homogenous_pointer{pointer.x, pointer.y, 0, 1};
  const glm::vec4 transformed_pointer = transform * homogenous_pointer;
  const glm::vec2 homogenized_transformed_pointer{escher::homogenize(transformed_pointer)};

  FX_VLOGS(2) << "Coordinate transform (device->view): (" << pointer.x << ", " << pointer.y
              << ")->(" << homogenized_transformed_pointer.x << ", "
              << homogenized_transformed_pointer.y << ")";

  return homogenized_transformed_pointer;
}

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
      return Phase::ADD;
    case PointerEventPhase::UP:
      return Phase::UP;
    case PointerEventPhase::MOVE:
      return Phase::CHANGE;
    case PointerEventPhase::DOWN:
      return Phase::DOWN;
    case PointerEventPhase::REMOVE:
      return Phase::REMOVE;
    case PointerEventPhase::CANCEL:
      return Phase::CANCEL;
    default:
      FX_CHECK(false) << "Should never be reached";
      return Phase::INVALID;
  }
}

PointerEventPhase InternalPhaseToGfxPhase(Phase phase) {
  switch (phase) {
    case Phase::ADD:
      return PointerEventPhase::ADD;
    case Phase::UP:
      return PointerEventPhase::UP;
    case Phase::CHANGE:
      return PointerEventPhase::MOVE;
    case Phase::DOWN:
      return PointerEventPhase::DOWN;
    case Phase::REMOVE:
      return PointerEventPhase::REMOVE;
    case Phase::CANCEL:
      return PointerEventPhase::CANCEL;
    case Phase::INVALID:
      FX_CHECK(false) << "Should never be reached.";
      return static_cast<PointerEventPhase>(0);
  };
}

std::vector<InternalPointerEvent> PointerInjectorEventToInternalPointerEvent(
    const fuchsia::ui::pointerinjector::Event& event, uint32_t device_id, const Viewport& viewport,
    zx_koid_t context, zx_koid_t target) {
  InternalPointerEvent internal_event;
  internal_event.timestamp = event.timestamp();
  internal_event.device_id = device_id;

  const fuchsia::ui::pointerinjector::PointerSample& pointer_sample = event.data().pointer_sample();
  internal_event.pointer_id = pointer_sample.pointer_id();
  internal_event.viewport = viewport;
  internal_event.position_in_viewport = {pointer_sample.position_in_viewport()[0],
                                         pointer_sample.position_in_viewport()[1]};
  internal_event.context = context;
  internal_event.target = target;
  if (event.has_trace_flow_id())
    internal_event.trace_id = event.trace_flow_id();

  std::vector<InternalPointerEvent> events;
  switch (pointer_sample.phase()) {
    case InjectorEventPhase::ADD: {
      // Insert extra event.
      InternalPointerEvent add_clone = internal_event;
      add_clone.phase = Phase::ADD;
      events.emplace_back(std::move(add_clone));
      internal_event.phase = Phase::DOWN;
      events.emplace_back(std::move(internal_event));
      break;
    }
    case InjectorEventPhase::CHANGE: {
      internal_event.phase = Phase::CHANGE;
      events.emplace_back(std::move(internal_event));
      break;
    }
    case InjectorEventPhase::REMOVE: {
      // Insert extra event.
      InternalPointerEvent up_clone = internal_event;
      up_clone.phase = Phase::UP;
      events.emplace_back(std::move(up_clone));
      internal_event.phase = Phase::REMOVE;
      events.emplace_back(std::move(internal_event));
      break;
    }
    case InjectorEventPhase::CANCEL: {
      internal_event.phase = Phase::CANCEL;
      events.emplace_back(std::move(internal_event));
      break;
    }
    default: {
      FX_CHECK(false) << "unsupported phase: " << static_cast<uint32_t>(pointer_sample.phase());
      break;
    }
  }

  return events;
}

InternalPointerEvent GfxPointerEventToInternalEvent(
    const fuchsia::ui::input::PointerEvent& event, zx_koid_t scene_koid, float screen_width,
    float screen_height, const glm::mat4& context_from_screen_transform) {
  InternalPointerEvent internal_event;
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
  internal_event.trace_id = PointerTraceHACK(event.radius_major, event.radius_minor);
  internal_event.phase = GfxPhaseToInternalPhase(event.phase);
  internal_event.buttons = event.buttons;

  return internal_event;
}

GfxPointerEvent InternalPointerEventToGfxPointerEvent(const InternalPointerEvent& internal_event,
                                                      const glm::mat4& view_from_context_transform,
                                                      fuchsia::ui::input::PointerEventType type) {
  GfxPointerEvent event;
  event.event_time = internal_event.timestamp;
  event.device_id = internal_event.device_id;
  event.pointer_id = internal_event.pointer_id;
  event.type = type;
  event.buttons = internal_event.buttons;

  // Convert to view-local coordinates.
  const glm::mat4 view_from_viewport_transform =
      view_from_context_transform * internal_event.viewport.context_from_viewport_transform;
  const glm::vec2 local_position =
      TransformPointerCoords(internal_event.position_in_viewport, view_from_viewport_transform);
  event.x = local_position.x;
  event.y = local_position.y;

  const auto [high, low] = ReversePointerTraceHACK(internal_event.trace_id);
  event.radius_minor = low;   // Lower 32 bits.
  event.radius_major = high;  // Upper 32 bits.

  event.phase = InternalPhaseToGfxPhase(internal_event.phase);

  return event;
}

glm::mat4 ColumnMajorMat3VectorToMat4(const std::array<float, 9>& matrix_array) {
  // clang-format off
  return glm::mat4(matrix_array[0], matrix_array[1], 0.f, matrix_array[2],  // first column
                   matrix_array[3], matrix_array[4], 0.f, matrix_array[5],  // second column
                               0.f,             0.f, 1.f,             0.f,  // third column
                   matrix_array[6], matrix_array[7], 0.f,             1.f); // fourth column
  // clang-format on
}

}  // namespace input
}  // namespace scenic_impl
