// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lib/ui/input/cpp/formatting.h"

#include <iomanip>
#include <iostream>

#include <fuchsia/ui/input/cpp/fidl.h>
#include "lib/fxl/strings/string_printf.h"

namespace fuchsia {
namespace ui {
namespace input {

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::InputEvent& value) {
  if (value.is_pointer()) {
    return os << value.pointer();
  } else if (value.is_keyboard()) {
    return os << value.keyboard();
  } else {
    return os;
  }
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::PointerEvent& value) {
  os << "{PointerEvent:";

  switch (value.phase) {
    case fuchsia::ui::input::PointerEventPhase::ADD:
      os << "ADD";
      break;
    case fuchsia::ui::input::PointerEventPhase::REMOVE:
      os << "REMOVE";
      break;
    case fuchsia::ui::input::PointerEventPhase::CANCEL:
      os << "CANCEL";
      break;
    case fuchsia::ui::input::PointerEventPhase::DOWN:
      os << "DOWN";
      break;
    case fuchsia::ui::input::PointerEventPhase::MOVE:
      os << "MOVE";
      break;
    case fuchsia::ui::input::PointerEventPhase::UP:
      os << "UP";
      break;
    case fuchsia::ui::input::PointerEventPhase::HOVER:
      os << "HOVER";
      break;
    default:
      os << "UNDEFINED";
  }

  os << ", device_id=" << value.device_id;
  os << ", pointer_id=" << value.pointer_id << ", type=";
  switch (value.type) {
    case fuchsia::ui::input::PointerEventType::TOUCH:
      os << "TOUCH";
      break;
    case fuchsia::ui::input::PointerEventType::STYLUS:
      os << "STYLUS";
      break;
    case fuchsia::ui::input::PointerEventType::INVERTED_STYLUS:
      os << "INVERTED_STYLUS";
      break;
    case fuchsia::ui::input::PointerEventType::MOUSE:
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

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardEvent& value) {
  os << "{KeyboardEvent:";

  switch (value.phase) {
    case fuchsia::ui::input::KeyboardEventPhase::PRESSED:
      os << "PRESSED";
      break;
    case fuchsia::ui::input::KeyboardEventPhase::RELEASED:
      os << "RELEASED";
      break;
    case fuchsia::ui::input::KeyboardEventPhase::CANCELLED:
      os << "CANCELLED";
      break;
    case fuchsia::ui::input::KeyboardEventPhase::REPEAT:
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
      if (value.modifiers & fuchsia::ui::input::kModifierCapsLock) {
        os << ":CAPS_LOCK";
      }
      if (value.modifiers & fuchsia::ui::input::kModifierShift) {
        os << ":SHIFT";
      }
      if (value.modifiers & fuchsia::ui::input::kModifierControl) {
        os << ":CONTROL";
      }
      if (value.modifiers & fuchsia::ui::input::kModifierAlt) {
        os << ":ALT";
      }
      if (value.modifiers & fuchsia::ui::input::kModifierSuper) {
        os << ":SUPER";
      }
    }
  }

  os << ", hid=" << fxl::StringPrintf("0x%08X", value.hid_usage);
  os << ", timestamp=" << value.event_time;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Range& value) {
  return os << "{Range[" << value.min << "," << value.max << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Axis& value) {
  return os << "{Axis: range=" << value.range
            << ", resolution=" << value.resolution << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardDescriptor& value) {
  os << "{Keyboard:";
  bool first = true;
  for (size_t index = 0; index < value.keys->size(); ++index) {
    if (first) {
      first = false;
      os << value.keys->at(index);
    } else {
      os << ", " << value.keys->at(index);
    }
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::MouseDescriptor& value) {
  os << "{Mouse:";
  os << "rel_x=" << value.rel_x;
  os << ", rel_y=" << value.rel_y;
  // TODO(jpoichet) vscroll, hscroll
  bool first = true;
  os << ", buttons=[";
  if (value.buttons & fuchsia::ui::input::kMouseButtonPrimary) {
    os << "PRIMARY";
    first = false;
  }
  if (value.buttons & fuchsia::ui::input::kMouseButtonSecondary) {
    if (first) {
      os << "SECONDARY";
      first = false;
    } else {
      os << ",SECONDARY";
    }
  }
  if (value.buttons & fuchsia::ui::input::kMouseButtonTertiary) {
    if (first) {
      os << "TERTIARY";
      first = false;
    } else {
      os << ",TERTIARY";
    }
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::StylusDescriptor& value) {
  os << "{Stylus:";
  os << "x=" << value.x;
  os << ", y=" << value.y;
  os << ", buttons=[";
  if (value.buttons & fuchsia::ui::input::kStylusBarrel) {
    os << "BARREL";
  }
  return os << "]}";
}

std::ostream& operator<<(
    std::ostream& os, const fuchsia::ui::input::TouchscreenDescriptor& value) {
  os << "{Touchscreen:";
  os << "x=" << value.x;
  os << ", y=" << value.y;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::SensorDescriptor& value) {
  os << "{Sensor:";
  os << "type=" << fidl::ToUnderlying(value.type);
  os << ", loc=" << fidl::ToUnderlying(value.loc);
  os << ", min_sampling_freq=" << value.min_sampling_freq;
  os << ", max_sampling_freq=" << value.max_sampling_freq;
  os << ", fifo_max_event_count=" << value.fifo_max_event_count;
  os << ", phys_min=" << value.phys_min;
  os << ", phys_max=" << value.phys_max;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::DeviceDescriptor& value) {
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
  if (value.sensor) {
    if (previous)
      os << ", ";
    os << *(value.sensor);
    previous = true;
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::KeyboardReport& value) {
  os << "{KeyboardReport: pressed_keys=[";
  bool first = true;
  for (size_t index = 0; index < value.pressed_keys->size(); ++index) {
    if (first) {
      first = false;
      os << value.pressed_keys->at(index);
    } else {
      os << ", " << value.pressed_keys->at(index);
    }
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::MouseReport& value) {
  os << "{MouseReport:";
  os << "rel_x=" << value.rel_x;
  os << ", rel_y=" << value.rel_y;
  // TODO(jpoichet) vscroll, hscroll
  os << ", pressed_buttons=" << value.pressed_buttons;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::StylusReport& value) {
  os << "{StylusReport:";
  os << "x=" << value.x;
  os << ", y=" << value.y;
  os << ", pressure=" << value.pressure;
  os << ", in_range=" << value.in_range;
  os << ", is_in_contact=" << value.is_in_contact;
  os << ", is_inverted=" << value.is_inverted;

  os << ", pressed_buttons=[";
  if (value.pressed_buttons & fuchsia::ui::input::kStylusBarrel) {
    os << "BARREL";
  }
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::Touch& value) {
  os << "{Touch:";
  os << "finger_id= " << value.finger_id;
  os << ", x=" << value.x;
  os << ", y=" << value.y;
  os << ", width=" << value.width;
  os << ", height=" << value.height;
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TouchscreenReport& value) {
  os << "{TouchscreenReport: touches=[";
  bool first = true;
  for (size_t index = 0; index < value.touches->size(); ++index) {
    if (first) {
      first = false;
      os << value.touches->at(index);
    } else {
      os << ", " << value.touches->at(index);
    }
  }

  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::SensorReport& value) {
  std::ios::fmtflags settings = os.flags();
  os << "{SensorReport: [" << std::hex << std::setfill('0');
  if (value.is_vector()) {
    const fidl::Array<int16_t, 3>& data = value.vector();
    for (size_t i = 0; i < data.count(); ++i) {
      os << "0x" << std::setw(4) << data[i];
      if (i + 1 < data.count())
        os << ",";
    }
  } else {
    os << "0x" << std::setw(4) << value.scalar();
  }
  os.flags(settings);
  return os << "]}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::InputReport& value) {
  os << "{InputReport: event_time=" << value.event_time << ",";

  if (value.keyboard) {
    os << *(value.keyboard);
  } else if (value.mouse) {
    os << *(value.mouse);
  } else if (value.stylus) {
    os << *(value.stylus);
  } else if (value.touchscreen) {
    os << *(value.touchscreen);
  } else if (value.sensor) {
    os << *(value.sensor);
  } else {
    os << "{Unknown Report}";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextSelection& value) {
  os << "{TextSelection: base=" << value.base << ", extent=" << value.extent
     << ", affinity=";
  switch (value.affinity) {
    case fuchsia::ui::input::TextAffinity::UPSTREAM:
      os << "UPSTREAM";
      break;
    case fuchsia::ui::input::TextAffinity::DOWNSTREAM:
      os << "DOWNSTREAM";
      break;
    default:
      os << "UNDEF";
  }
  return os << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextRange& value) {
  return os << "{TextRange: start=" << value.start << ", end=" << value.end
            << "}";
}

std::ostream& operator<<(std::ostream& os,
                         const fuchsia::ui::input::TextInputState& value) {
  os << "{TextInputState: revision=" << value.revision;
  os << ", text='" << value.text << "'";
  os << ", selection=" << value.selection;
  os << ", composing=" << value.composing;
  return os << "}";
}

}  // namespace input
}  // namespace ui
}  // namespace fuchsia
