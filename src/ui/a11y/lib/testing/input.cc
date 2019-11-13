// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/a11y/lib/testing/input.h"

namespace accessibility_test {

using fuchsia::ui::input::PointerEventPhase;

PointerParams::PointerParams(PointerId pointer_id, PointerEventPhase phase,
                             const glm::vec2& coordinate)
    : pointer_id(pointer_id), phase(phase), coordinate(coordinate) {}

std::vector<PointerParams> DownEvents(PointerId pointer_id, const glm::vec2& coordinate) {
  return {{pointer_id, PointerEventPhase::ADD, coordinate},
          {pointer_id, PointerEventPhase::DOWN, coordinate}};
}

std::vector<PointerParams> UpEvents(PointerId pointer_id, const glm::vec2& coordinate) {
  return {{pointer_id, PointerEventPhase::UP, coordinate},
          {pointer_id, PointerEventPhase::REMOVE, coordinate}};
}

std::vector<PointerParams> TapEvents(PointerId pointer_id, const glm::vec2& coordinate) {
  return DownEvents(pointer_id, coordinate) + UpEvents(pointer_id, coordinate);
}

// Pointer move events between two endpoints, (start, end]. The start point is exclusive and the end
// point is inclusive, as move events signify where a pointer has moved to rather than where it has
// moved from.
std::vector<PointerParams> MoveEvents(PointerId pointer_id, const glm::vec2& start,
                                      const glm::vec2& end, size_t moves) {
  std::vector<PointerParams> events;
  events.reserve(moves);
  for (size_t i = 1; i <= moves; ++i) {
    events.emplace_back(pointer_id, PointerEventPhase::MOVE,
                        start + (end - start) * static_cast<float>(i) / static_cast<float>(moves));
  }

  return events;
}

std::vector<PointerParams> DragEvents(PointerId pointer_id, const glm::vec2& start,
                                      const glm::vec2& end, size_t moves) {
  return DownEvents(pointer_id, start) + MoveEvents(pointer_id, start, end, moves) +
         UpEvents(pointer_id, end);
}

fuchsia::ui::input::accessibility::PointerEvent ToPointerEvent(const PointerParams& params,
                                                               uint64_t event_time) {
  fuchsia::ui::input::accessibility::PointerEvent event;
  event.set_event_time(event_time);
  event.set_device_id(1);
  event.set_pointer_id(params.pointer_id);
  event.set_type(fuchsia::ui::input::PointerEventType::TOUCH);
  event.set_phase(params.phase);
  event.set_ndc_point({params.coordinate.x, params.coordinate.y});

  return event;
}

}  // namespace accessibility_test
