// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/scenic/integration_tests/utils.h"

#include <lib/syslog/cpp/macros.h>
#include <lib/ui/scenic/cpp/id.h>

#include <cmath>

namespace integration_tests {

using InputCommand = fuchsia::ui::input::Command;
using fuchsia::ui::input::PointerEvent;
using fuchsia::ui::input::PointerEventPhase;
using fuchsia::ui::input::PointerEventType;

namespace {
std::ostream& operator<<(std::ostream& os, const PointerEventPhase& value) {
  switch (value) {
    case PointerEventPhase::ADD:
      return os << "add";
    case PointerEventPhase::HOVER:
      return os << "hover";
    case PointerEventPhase::DOWN:
      return os << "down";
    case PointerEventPhase::MOVE:
      return os << "move";
    case PointerEventPhase::UP:
      return os << "up";
    case PointerEventPhase::REMOVE:
      return os << "remove";
    case PointerEventPhase::CANCEL:
      return os << "cancel";
    default:
      return os << "<invalid enum value: " << static_cast<uint32_t>(value) << ">";
  }
}

std::ostream& operator<<(std::ostream& os, const PointerEventType& value) {
  switch (value) {
    case PointerEventType::TOUCH:
      return os << "touch";
    case PointerEventType::STYLUS:
      return os << "stylus";
    case PointerEventType::INVERTED_STYLUS:
      return os << "inverted stylus";
    case PointerEventType::MOUSE:
      return os << "mouse";
    default:
      return os << "<invalid enum value: " << static_cast<uint32_t>(value) << ">";
  }
}

}  // namespace

// Used to compare whether two values are nearly equal.
// 1000 times machine limits to account for scaling from [0,1] to viewing volume [0,1000].
constexpr float kEpsilon = std::numeric_limits<float>::epsilon() * 1000;

bool PointerMatches(const PointerEvent& event, uint32_t pointer_id, PointerEventPhase phase,
                    float x, float y, PointerEventType type, uint32_t buttons) {
  bool result = true;
  if (event.type != type) {
    FX_LOGS(ERROR) << "  Actual type: " << event.type;
    FX_LOGS(ERROR) << "Expected type: " << type;
    result = false;
  }
  if (event.buttons != buttons) {
    FX_LOGS(ERROR) << "  Actual buttons: " << event.buttons;
    FX_LOGS(ERROR) << "Expected buttons: " << buttons;
    result = false;
  }
  if (event.pointer_id != pointer_id) {
    FX_LOGS(ERROR) << "  Actual id: " << event.pointer_id;
    FX_LOGS(ERROR) << "Expected id: " << pointer_id;
    result = false;
  }
  if (event.phase != phase) {
    FX_LOGS(ERROR) << "  Actual phase: " << event.phase;
    FX_LOGS(ERROR) << "Expected phase: " << phase;
    result = false;
  }
  if (fabs(event.x - x) > kEpsilon) {
    FX_LOGS(ERROR) << "  Actual x: " << event.x;
    FX_LOGS(ERROR) << "Expected x: " << x;
    result = false;
  }
  if (fabs(event.y - y) > kEpsilon) {
    FX_LOGS(ERROR) << "  Actual y: " << event.y;
    FX_LOGS(ERROR) << "Expected y: " << y;
    result = false;
  }
  return result;
}

bool CmpFloatingValues(float num1, float num2) {
  auto diff = fabs(num1 - num2);
  return diff < kEpsilon;
}

zx_koid_t ExtractKoid(const zx::object_base& object) {
  zx_info_handle_basic_t info{};
  if (object.get_info(ZX_INFO_HANDLE_BASIC, &info, sizeof(info), nullptr, nullptr) != ZX_OK) {
    return ZX_KOID_INVALID;  // no info
  }

  return info.koid;
}

zx_koid_t ExtractKoid(const fuchsia::ui::views::ViewRef& view_ref) {
  return ExtractKoid(view_ref.reference);
}

PointerCommandGenerator::PointerCommandGenerator(uint32_t compositor_id, uint32_t device_id,
                                                 uint32_t pointer_id, PointerEventType type,
                                                 uint32_t buttons)
    : compositor_id_(compositor_id) {
  blank_.device_id = device_id;
  blank_.pointer_id = pointer_id;
  blank_.type = type;
  blank_.buttons = buttons;
}

InputCommand PointerCommandGenerator::Add(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::ADD;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Down(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::DOWN;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Move(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::MOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Up(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::UP;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::Remove(float x, float y) {
  PointerEvent event;
  fidl::Clone(blank_, &event);
  event.phase = PointerEventPhase::REMOVE;
  event.x = x;
  event.y = y;
  return MakeInputCommand(event);
}

InputCommand PointerCommandGenerator::MakeInputCommand(PointerEvent event) {
  fuchsia::ui::input::SendPointerInputCmd pointer_cmd;
  pointer_cmd.compositor_id = compositor_id_;
  pointer_cmd.pointer_event = std::move(event);

  InputCommand input_cmd;
  input_cmd.set_send_pointer_input(std::move(pointer_cmd));

  return input_cmd;
}

}  // namespace integration_tests
