// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/input/cpp/formatting.h"

#include <iostream>

#include "lib/ftl/strings/string_printf.h"

namespace mozart {

std::ostream& operator<<(std::ostream& os, const InputEvent& value) {
  if (value.is_pointer()) {
    return os << *(value.get_pointer().get());
  } else if (value.is_keyboard()) {
    return os << *(value.get_keyboard().get());
  } else {
    return os;
  }
}

std::ostream& operator<<(std::ostream& os, const PointerEvent& value) {
  os << "{PointerEvent:";

  switch (value.phase) {
    case mozart::PointerEvent::Phase::ADD:
      os << "ADD";
      break;
    case mozart::PointerEvent::Phase::REMOVE:
      os << "REMOVE";
      break;
    case mozart::PointerEvent::Phase::CANCEL:
      os << "CANCEL";
      break;
    case mozart::PointerEvent::Phase::DOWN:
      os << "DOWN";
      break;
    case mozart::PointerEvent::Phase::MOVE:
      os << "MOVE";
      break;
    case mozart::PointerEvent::Phase::UP:
      os << "UP";
      break;
    case mozart::PointerEvent::Phase::HOVER:
      os << "HOVER";
      break;
    default:
      os << "UNDEFINED";
  }

  os << ", device_id=" << value.device_id;
  os << ", pointer_id=" << value.pointer_id << ", type=";
  switch (value.type) {
    case mozart::PointerEvent::Type::TOUCH:
      os << "TOUCH";
      break;
    case mozart::PointerEvent::Type::STYLUS:
      os << "STYLUS";
      break;
    case mozart::PointerEvent::Type::INVERTED_STYLUS:
      os << "INVERTED_STYLUS";
      break;
    case mozart::PointerEvent::Type::MOUSE:
      os << "MOUSE";
      break;
    default:
      os << "UNDEFINED";
  }
  os << ", x=" << value.x << ", y=" << value.y;
  os << ", buttons = " << ftl::StringPrintf("0x%08X", value.buttons);
  os << ", timestamp=" << value.event_time;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const KeyboardEvent& value) {
  os << "{KeyboardEvent:";

  switch (value.phase) {
    case mozart::KeyboardEvent::Phase::PRESSED:
      os << "PRESSED";
      break;
    case mozart::KeyboardEvent::Phase::RELEASED:
      os << "RELEASED";
      break;
    case mozart::KeyboardEvent::Phase::CANCELLED:
      os << "CANCELLED";
      break;
    case mozart::KeyboardEvent::Phase::REPEAT:
      os << "REPEAT";
      break;
    default:
      os << "UNDEFINED";
  }

  os << ", device_id=" << value.device_id;
  if (value.code_point) {
    os << ", character=" << value.code_point;
    if (value.modifiers) {
      os << ", modifiers";
      if (value.modifiers & mozart::kModifierCapsLock) {
        os << ":CAPS_LOCK";
      }
      if (value.modifiers & mozart::kModifierShift) {
        os << ":SHIFT";
      }
      if (value.modifiers & mozart::kModifierControl) {
        os << ":CONTROL";
      }
      if (value.modifiers & mozart::kModifierAlt) {
        os << ":ALT";
      }
      if (value.modifiers & mozart::kModifierSuper) {
        os << ":SUPER";
      }
    }
  }

  os << ", hid=" << ftl::StringPrintf("0x%08X", value.hid_usage);
  os << ", timestamp=" << value.event_time;
  return os << "}";
}

}  // namespace mozart
