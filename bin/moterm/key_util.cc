// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/moterm/key_util.h"

#include <hid/usages.h>

#include "lib/ui/input/fidl/input_events.fidl.h"
#include "lib/fxl/logging.h"

// TODO(vtl): Handle more stuff and verify that we're consistent about the
// sequences we generate.
// TODO(vtl): In particular, our implementation of keypad_application_mode is
// incomplete.
std::string GetInputSequenceForKeyPressedEvent(
    const mozart::InputEvent& key_event,
    bool keypad_application_mode) {
  if (key_event.is_keyboard()) {
    const mozart::KeyboardEventPtr& keyboard = key_event.get_keyboard();
    FXL_DCHECK(keyboard->phase == mozart::KeyboardEvent::Phase::PRESSED ||
               keyboard->phase == mozart::KeyboardEvent::Phase::REPEAT);

    if (keyboard->code_point) {
      if (keyboard->code_point > 128) {
        FXL_NOTIMPLEMENTED();
        return std::string();
      }

      uint32_t non_control = (mozart::kModifierShift | mozart::kModifierAlt |
                              mozart::kModifierSuper);
      if (keyboard->modifiers) {
        if ((keyboard->modifiers & mozart::kModifierControl) &&
            !(keyboard->modifiers & non_control)) {
          char c = static_cast<char>(keyboard->code_point);
          if (c >= 'a' && c <= 'z') {
            c -= 96;
          } else if (c >= '@' && c <= '_') {
            c -= 64;
          }
          return std::string(1, c);
        }
      }
      return std::string(1, static_cast<char>(keyboard->code_point));
    }

    switch (keyboard->hid_usage) {
      case HID_USAGE_KEY_BACKSPACE:
        // Have backspace send DEL instead of BS.
        return std::string("\x7f");
      case HID_USAGE_KEY_ESC:
        return std::string("\33");
      case HID_USAGE_KEY_PAGEDOWN:
        return std::string("\33[6~");
      case HID_USAGE_KEY_PAGEUP:
        return std::string("\33[5~");
      case HID_USAGE_KEY_END:
        return std::string("\33[F");
      case HID_USAGE_KEY_HOME:
        return std::string("\33[H");
      case HID_USAGE_KEY_LEFT:
        return std::string("\33[D");
      case HID_USAGE_KEY_UP:
        return std::string("\33[A");
      case HID_USAGE_KEY_RIGHT:
        return std::string("\33[C");
      case HID_USAGE_KEY_DOWN:
        return std::string("\33[B");
      case HID_USAGE_KEY_INSERT:
        return std::string("\33[2~");
      case HID_USAGE_KEY_DELETE:
        return std::string("\33[3~");
      case HID_USAGE_KEY_ENTER:
        return std::string("\n");
      case HID_USAGE_KEY_TAB:
        return std::string("\t");
      default:
        FXL_NOTIMPLEMENTED() << " hid_usage = " << keyboard->hid_usage;
    }
  }
  return std::string();
}
