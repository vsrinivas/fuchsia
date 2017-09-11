// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/cpp/formatting.h"

#include <iostream>

#include "lib/ui/input/fidl/usages.fidl.h"
#include "lib/fxl/strings/string_printf.h"

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
  os << ", buttons = " << fxl::StringPrintf("0x%08X", value.buttons);
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

  os << ", hid=" << fxl::StringPrintf("0x%08X", value.hid_usage);
  os << ", timestamp=" << value.event_time;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const Range& value) {
  return os << "{Range[" << value.min << "," << value.max << "]}";
}

std::ostream& operator<<(std::ostream& os, const Axis& value) {
  return os << "{Axis: range=" << *(value.range)
            << ", resolution=" << value.resolution << "}";
}

std::ostream& operator<<(std::ostream& os, const KeyboardDescriptor& value) {
  os << "{Keyboard:";
  bool first = true;
  for (size_t index = 0; index < value.keys.size(); ++index) {
    if (first) {
      first = false;
      os << value.keys[index];
    } else {
      os << ", " << value.keys[index];
    }
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const MouseDescriptor& value) {
  os << "{Mouse:";
  os << "rel_x=" << *(value.rel_x);
  os << ", rel_y=" << *(value.rel_y);
  // TODO(jpoichet) vscroll, hscroll
  bool first = true;
  os << ", buttons=[";
  if (value.buttons & kMouseButtonPrimary) {
    os << "PRIMARY";
    first = false;
  }
  if (value.buttons & kMouseButtonSecondary) {
    if (first) {
      os << "SECONDARY";
      first = false;
    } else {
      os << ",SECONDARY";
    }
  }
  if (value.buttons & kMouseButtonTertiary) {
    if (first) {
      os << "TERTIARY";
      first = false;
    } else {
      os << ",TERTIARY";
    }
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const StylusDescriptor& value) {
  os << "{Stylus:";
  os << "x=" << *(value.x);
  os << ", y=" << *(value.y);
  os << ", buttons=[";
  if (value.buttons & kStylusBarrel) {
    os << "BARREL";
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const TouchscreenDescriptor& value) {
  os << "{Touchscreen:";
  os << "x=" << *(value.x);
  os << ", y=" << *(value.y);
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const DeviceDescriptor& value) {
  os << "{DeviceDescriptor:";
  bool previous = false;
  if (value.keyboard) {
    os << *(value.keyboard);
    previous = true;
  }
  if (value.mouse) {
    if (previous)
      os << ", ";
    os << *(value.mouse);
    previous = true;
  }
  if (value.stylus) {
    if (previous)
      os << ", ";
    os << *(value.stylus);
    previous = true;
  }
  if (value.touchscreen) {
    if (previous)
      os << ", ";
    os << *(value.touchscreen);
    previous = true;
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const KeyboardReport& value) {
  os << "{KeyboardReport: pressed_keys=[";
  bool first = true;
  for (size_t index = 0; index < value.pressed_keys.size(); ++index) {
    if (first) {
      first = false;
      os << value.pressed_keys[index];
    } else {
      os << ", " << value.pressed_keys[index];
    }
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const MouseReport& value) {
  os << "{MouseReport:";
  os << "rel_x=" << value.rel_x;
  os << ", rel_y=" << value.rel_y;
  // TODO(jpoichet) vscroll, hscroll
  os << ", pressed_buttons=" << value.pressed_buttons;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const StylusReport& value) {
  os << "{StylusReport:";
  os << "x=" << value.x;
  os << ", y=" << value.y;
  os << ", pressure=" << value.pressure;
  os << ", in_range=" << value.in_range;
  os << ", is_in_contact=" << value.is_in_contact;
  os << ", is_inverted=" << value.is_inverted;

  os << ", pressed_buttons=[";
  if (value.pressed_buttons & kStylusBarrel) {
    os << "BARREL";
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const Touch& value) {
  os << "{Touch:";
  os << "finger_id= " << value.finger_id;
  os << ", x=" << value.x;
  os << ", y=" << value.y;
  os << ", width=" << value.width;
  os << ", height=" << value.height;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const TouchscreenReport& value) {
  os << "{TouchscreenReport: touches=[";
  bool first = true;
  for (size_t index = 0; index < value.touches.size(); ++index) {
    if (first) {
      first = false;
      os << *(value.touches[index]);
    } else {
      os << ", " << *(value.touches[index]);
    }
  }

  return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const InputReport& value) {
  os << "{InputReport: event_time=" << value.event_time << ",";

  if (value.keyboard) {
    os << *(value.keyboard);
  } else if (value.mouse) {
    os << *(value.mouse);
  } else if (value.stylus) {
    os << *(value.stylus);
  } else if (value.touchscreen) {
    os << *(value.touchscreen);
  } else {
    os << "{Unknown Report}";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const TextSelection& value) {
  return os << "{TextSelection: base=" << value.base
            << ", extent=" << value.extent << ", affinity=";
  switch (value.affinity) {
    case mozart::TextAffinity::UPSTREAM:
      os << "UPSTREAM";
      break;
    case mozart::TextAffinity::DOWNSTREAM:
      os << "DOWNSTREAM";
      break;
    default:
      os << "UNDEF";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os, const TextRange& value) {
  return os << "{TextRange: start=" << value.start << ", end=" << value.end
            << "}";
}

std::ostream& operator<<(std::ostream& os, const TextInputState& value) {
  os << "{TextInputState: revision=" << value.revision;
  os << ", text='" << value.text << "'";
  os << ", selection=" << *(value.selection);
  os << ", composing=" << *(value.composing);
  return os << "}";
}

}  // namespace mozart
