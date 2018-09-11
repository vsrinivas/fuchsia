// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/input/tests/util.h"

#include "lib/fidl/cpp/clone.h"

namespace lib_ui_input_tests {

using fuchsia::ui::input::Command;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;
using fuchsia::ui::input::SendPointerInputCmd;
using scenic::ResourceId;

PointerEventGenerator::PointerEventGenerator(ResourceId compositor_id,
                                             uint32_t device_id,
                                             uint32_t pointer_id,
                                             PointerEventType type) {
  compositor_id_ = compositor_id;
  blank_.device_id = device_id;
  blank_.pointer_id = pointer_id;
  blank_.type = type;
}

fuchsia::ui::input::Command PointerEventGenerator::Add(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::ADD;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Down(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::DOWN;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Move(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::MOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Up(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::UP;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::Remove(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::REMOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

fuchsia::ui::input::Command PointerEventGenerator::MakeInputCommand(
    PointerEvent event) {
  SendPointerInputCmd pointer_cmd;
  pointer_cmd.compositor_id = compositor_id_;
  pointer_cmd.pointer_event = std::move(event);

  Command input_cmd;
  input_cmd.set_send_pointer_input(std::move(pointer_cmd));

  return input_cmd;
}

}  // namespace lib_ui_input_tests

