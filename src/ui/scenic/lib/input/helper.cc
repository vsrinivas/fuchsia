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

// Finds (Vulkan) normalized device coordinates with respect to the (single) layer. This is intended
// for magnification gestures.
glm::vec2 NormalizePointerCoords(const glm::vec2& pointer, const gfx::LayerStackPtr& layer_stack) {
  if (layer_stack->layers().empty()) {
    return {0, 0};
  }

  // RootPresenter only owns one layer per presentation/layer stack. To support multiple layers,
  // we'd need to partition the input space. So, for now to simplify things we'll treat the layer
  // size as display dimensions, and if we ever find more than one layer in a stack, we should
  // worry.
  FX_DCHECK(layer_stack->layers().size() == 1)
      << "Multiple GFX layers; multi-layer input dispatch not implemented.";
  const gfx::Layer& layer = **layer_stack->layers().begin();

  return {
      layer.width() > 0 ? 2.f * pointer.x / layer.width() - 1 : 0,
      layer.height() > 0 ? 2.f * pointer.y / layer.height() - 1 : 0,
  };
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

std::vector<GfxPointerEvent> PointerInjectorEventToGfxPointerEvent(
    const fuchsia::ui::pointerinjector::Event& event, uint32_t device_id,
    const glm::mat3& viewport_to_context_transform) {
  const fuchsia::ui::pointerinjector::PointerSample& pointer_sample = event.data().pointer_sample();
  GfxPointerEvent pointer_event;
  pointer_event.type = fuchsia::ui::input::PointerEventType::TOUCH;
  pointer_event.device_id = device_id;

  pointer_event.event_time = event.timestamp();
  pointer_event.pointer_id = pointer_sample.pointer_id();

  // Convert to context-local coordinates.
  const glm::vec3 viewport_position = {pointer_sample.position_in_viewport()[0],
                                       pointer_sample.position_in_viewport()[1],
                                       1};  // homogeneous coordinates
  const glm::vec3 context_position = viewport_to_context_transform * viewport_position;
  pointer_event.x = context_position.x;
  pointer_event.y = context_position.y;

  if (event.has_trace_flow_id()) {
    const auto [high, low] = ReversePointerTraceHACK(event.trace_flow_id());
    pointer_event.radius_minor = low;   // Lower 32 bits.
    pointer_event.radius_major = high;  // Upper 32 bits.
  }

  std::vector<GfxPointerEvent> events;
  switch (pointer_sample.phase()) {
    case InjectorEventPhase::ADD: {
      GfxPointerEvent down;
      fidl::Clone(pointer_event, &down);
      pointer_event.phase = PointerEventPhase::ADD;
      down.phase = PointerEventPhase::DOWN;
      events.emplace_back(std::move(pointer_event));
      events.emplace_back(std::move(down));
      break;
    }
    case InjectorEventPhase::CHANGE: {
      pointer_event.phase = PointerEventPhase::MOVE;
      events.emplace_back(std::move(pointer_event));
      break;
    }
    case InjectorEventPhase::REMOVE: {
      GfxPointerEvent up;
      fidl::Clone(pointer_event, &up);
      up.phase = PointerEventPhase::UP;
      pointer_event.phase = PointerEventPhase::REMOVE;
      events.emplace_back(std::move(up));
      events.emplace_back(std::move(pointer_event));
      break;
    }
    case InjectorEventPhase::CANCEL: {
      pointer_event.phase = PointerEventPhase::CANCEL;
      events.emplace_back(std::move(pointer_event));
      break;
    }
    default:
      FX_CHECK(false) << "unknown phase";
      break;
  }

  return events;
}

glm::mat3 ColumnMajorVectorToMat3(const std::array<float, 9>& matrix_array) {
  // clang-format off
  return glm::mat3(matrix_array[0], matrix_array[1], matrix_array[2], // first column
                   matrix_array[3], matrix_array[4], matrix_array[5], // second column
                   matrix_array[6], matrix_array[7], matrix_array[8]); // third column
  // clang-format on
}

}  // namespace input
}  // namespace scenic_impl
