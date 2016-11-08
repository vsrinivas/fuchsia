// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/services/input/cpp/formatting.h"

#include <iostream>

#include "lib/ftl/strings/string_printf.h"

namespace mozart {

std::ostream& operator<<(std::ostream& os, const KeyData& value) {
  if (value.code_point) {
    return os << "{character=" << value.code_point << "}";
  } else {
    return os << "{hid=" << ftl::StringPrintf("0x%08X", value.hid_usage) << "}";
  }
}

std::ostream& operator<<(std::ostream& os, const PointerData& value) {
  os << "{pointer_id=" << value.pointer_id << ", kind=";
  switch (value.kind) {
    case mozart::PointerKind::TOUCH:
      os << "TOUCH";
      break;
    case mozart::PointerKind::MOUSE:
      os << "MOUSE";
      break;
    default:
      os << "UNDEFINED";
  }
  os << ", x=" << value.x << ", y=" << value.y;
  if (value.pressure != 0) {
    return os << ", pressure=" << value.pressure;
  } else if (value.radius_major != 0 || value.radius_minor != 0) {
    return os << ", radius="
              << ftl::StringPrintf("%.2fx%.2f", value.radius_minor,
                                   value.radius_major);
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const Event& value) {
  os << "{";
  os << "action=";

  switch (value.action) {
    case mozart::EventType::UNKNOWN:
      os << "UNKNOWN";
      break;
    case mozart::EventType::KEY_PRESSED:
      os << "KEY_PRESSED";
      break;
    case mozart::EventType::KEY_RELEASED:
      os << "KEY_RELEASED";
      break;
    case mozart::EventType::POINTER_CANCEL:
      os << "POINTER_CANCEL";
      break;
    case mozart::EventType::POINTER_DOWN:
      os << "POINTER_DOWN";
      break;
    case mozart::EventType::POINTER_MOVE:
      os << "POINTER_MOVE";
      break;
    case mozart::EventType::POINTER_UP:
      os << "POINTER_UP";
      break;
    default:
      os << "UNDEFINED";
  }

  os << ", flags=";
  switch (value.flags) {
    case mozart::EventFlags::NONE:
      os << "NONE";
      break;
    case mozart::EventFlags::CAPS_LOCK_DOWN:
      os << "CAPS_LOCK_DOWN";
      break;
    case mozart::EventFlags::SHIFT_DOWN:
      os << "SHIFT_DOWN";
      break;
    case mozart::EventFlags::CONTROL_DOWN:
      os << "CONTROL_DOWN";
      break;
    case mozart::EventFlags::ALT_DOWN:
      os << "ALT_DOWN";
      break;
    case mozart::EventFlags::LEFT_MOUSE_BUTTON:
      os << "LEFT_MOUSE_BUTTON";
      break;
    case mozart::EventFlags::MIDDLE_MOUSE_BUTTON:
      os << "MIDDLE_MOUSE_BUTTON";
      break;
    case mozart::EventFlags::RIGHT_MOUSE_BUTTON:
      os << "RIGHT_MOUSE_BUTTON";
      break;
    case mozart::EventFlags::COMMAND_DOWN:
      os << "COMMAND_DOWN";
      break;
    case mozart::EventFlags::EXTENDED:
      os << "EXTENDED";
      break;
    case mozart::EventFlags::IS_SYNTHESIZED:
      os << "IS_SYNTHESIZED";
      break;
    case mozart::EventFlags::ALTGR_DOWN:
      os << "ALTGR_DOWN";
      break;
    case mozart::EventFlags::MOD3_DOWN:
      os << "MOD3_DOWN";
      break;
    default:
      os << "UNDEFINED";
  }

  os << ", timestamp=" << value.time_stamp;
  if (value.key_data) {
    os << ", key_data=" << *value.key_data;
  }
  if (value.pointer_data) {
    os << ", pointer_data=" << *value.pointer_data;
  }
  return os << "}";
}

}  // namespace mozart
