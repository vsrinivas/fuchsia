// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/key_util/key_util.h"

#include <iostream>

#include "hid-parser/usages.h"
#include "hid/usages.h"

namespace key_util {

constexpr std::pair<uint32_t, fuchsia::ui::input2::Key> key_map[] = {
    {HID_USAGE_KEY_A, fuchsia::ui::input2::Key::A},
    {HID_USAGE_KEY_B, fuchsia::ui::input2::Key::B},
    {HID_USAGE_KEY_C, fuchsia::ui::input2::Key::C},
    {HID_USAGE_KEY_D, fuchsia::ui::input2::Key::D},
    {HID_USAGE_KEY_E, fuchsia::ui::input2::Key::E},
    {HID_USAGE_KEY_F, fuchsia::ui::input2::Key::F},
    {HID_USAGE_KEY_G, fuchsia::ui::input2::Key::G},
    {HID_USAGE_KEY_H, fuchsia::ui::input2::Key::H},
    {HID_USAGE_KEY_I, fuchsia::ui::input2::Key::I},
    {HID_USAGE_KEY_J, fuchsia::ui::input2::Key::J},
    {HID_USAGE_KEY_K, fuchsia::ui::input2::Key::K},
    {HID_USAGE_KEY_L, fuchsia::ui::input2::Key::L},
    {HID_USAGE_KEY_M, fuchsia::ui::input2::Key::M},
    {HID_USAGE_KEY_N, fuchsia::ui::input2::Key::N},
    {HID_USAGE_KEY_O, fuchsia::ui::input2::Key::O},
    {HID_USAGE_KEY_P, fuchsia::ui::input2::Key::P},
    {HID_USAGE_KEY_Q, fuchsia::ui::input2::Key::Q},
    {HID_USAGE_KEY_R, fuchsia::ui::input2::Key::R},
    {HID_USAGE_KEY_S, fuchsia::ui::input2::Key::S},
    {HID_USAGE_KEY_T, fuchsia::ui::input2::Key::T},
    {HID_USAGE_KEY_U, fuchsia::ui::input2::Key::U},
    {HID_USAGE_KEY_V, fuchsia::ui::input2::Key::V},
    {HID_USAGE_KEY_W, fuchsia::ui::input2::Key::W},
    {HID_USAGE_KEY_X, fuchsia::ui::input2::Key::X},
    {HID_USAGE_KEY_Y, fuchsia::ui::input2::Key::Y},
    {HID_USAGE_KEY_Z, fuchsia::ui::input2::Key::Z},
    {HID_USAGE_KEY_1, fuchsia::ui::input2::Key::KEY_1},
    {HID_USAGE_KEY_2, fuchsia::ui::input2::Key::KEY_2},
    {HID_USAGE_KEY_3, fuchsia::ui::input2::Key::KEY_3},
    {HID_USAGE_KEY_4, fuchsia::ui::input2::Key::KEY_4},
    {HID_USAGE_KEY_5, fuchsia::ui::input2::Key::KEY_5},
    {HID_USAGE_KEY_6, fuchsia::ui::input2::Key::KEY_6},
    {HID_USAGE_KEY_7, fuchsia::ui::input2::Key::KEY_7},
    {HID_USAGE_KEY_8, fuchsia::ui::input2::Key::KEY_8},
    {HID_USAGE_KEY_9, fuchsia::ui::input2::Key::KEY_9},
    {HID_USAGE_KEY_0, fuchsia::ui::input2::Key::KEY_0},
    {HID_USAGE_KEY_ENTER, fuchsia::ui::input2::Key::ENTER},
    {HID_USAGE_KEY_ESC, fuchsia::ui::input2::Key::ESCAPE},
    {HID_USAGE_KEY_BACKSPACE, fuchsia::ui::input2::Key::BACKSPACE},
    {HID_USAGE_KEY_TAB, fuchsia::ui::input2::Key::TAB},
    {HID_USAGE_KEY_SPACE, fuchsia::ui::input2::Key::SPACE},
    {HID_USAGE_KEY_MINUS, fuchsia::ui::input2::Key::MINUS},
    {HID_USAGE_KEY_EQUAL, fuchsia::ui::input2::Key::EQUALS},
    {HID_USAGE_KEY_LEFTBRACE, fuchsia::ui::input2::Key::LEFT_BRACE},
    {HID_USAGE_KEY_RIGHTBRACE, fuchsia::ui::input2::Key::RIGHT_BRACE},
    {HID_USAGE_KEY_BACKSLASH, fuchsia::ui::input2::Key::BACKSLASH},
    {HID_USAGE_KEY_NON_US_OCTOTHORPE, fuchsia::ui::input2::Key::NON_US_HASH},
    {HID_USAGE_KEY_SEMICOLON, fuchsia::ui::input2::Key::SEMICOLON},
    {HID_USAGE_KEY_APOSTROPHE, fuchsia::ui::input2::Key::APOSTROPHE},
    {HID_USAGE_KEY_GRAVE, fuchsia::ui::input2::Key::GRAVE_ACCENT},
    {HID_USAGE_KEY_COMMA, fuchsia::ui::input2::Key::COMMA},
    {HID_USAGE_KEY_DOT, fuchsia::ui::input2::Key::DOT},
    {HID_USAGE_KEY_SLASH, fuchsia::ui::input2::Key::SLASH},
    {HID_USAGE_KEY_CAPSLOCK, fuchsia::ui::input2::Key::CAPS_LOCK},
    {HID_USAGE_KEY_F1, fuchsia::ui::input2::Key::F1},
    {HID_USAGE_KEY_F2, fuchsia::ui::input2::Key::F2},
    {HID_USAGE_KEY_F3, fuchsia::ui::input2::Key::F3},
    {HID_USAGE_KEY_F4, fuchsia::ui::input2::Key::F4},
    {HID_USAGE_KEY_F5, fuchsia::ui::input2::Key::F5},
    {HID_USAGE_KEY_F6, fuchsia::ui::input2::Key::F6},
    {HID_USAGE_KEY_F7, fuchsia::ui::input2::Key::F7},
    {HID_USAGE_KEY_F8, fuchsia::ui::input2::Key::F8},
    {HID_USAGE_KEY_F9, fuchsia::ui::input2::Key::F9},
    {HID_USAGE_KEY_F10, fuchsia::ui::input2::Key::F10},
    {HID_USAGE_KEY_F11, fuchsia::ui::input2::Key::F11},
    {HID_USAGE_KEY_F12, fuchsia::ui::input2::Key::F12},
    {HID_USAGE_KEY_PRINTSCREEN, fuchsia::ui::input2::Key::PRINT_SCREEN},
    {HID_USAGE_KEY_SCROLLLOCK, fuchsia::ui::input2::Key::SCROLL_LOCK},
    {HID_USAGE_KEY_PAUSE, fuchsia::ui::input2::Key::PAUSE},
    {HID_USAGE_KEY_INSERT, fuchsia::ui::input2::Key::INSERT},
    {HID_USAGE_KEY_HOME, fuchsia::ui::input2::Key::HOME},
    {HID_USAGE_KEY_PAGEUP, fuchsia::ui::input2::Key::PAGE_UP},
    {HID_USAGE_KEY_DELETE, fuchsia::ui::input2::Key::DELETE},
    {HID_USAGE_KEY_END, fuchsia::ui::input2::Key::END},
    {HID_USAGE_KEY_PAGEDOWN, fuchsia::ui::input2::Key::PAGE_DOWN},
    {HID_USAGE_KEY_RIGHT, fuchsia::ui::input2::Key::RIGHT},
    {HID_USAGE_KEY_LEFT, fuchsia::ui::input2::Key::LEFT},
    {HID_USAGE_KEY_DOWN, fuchsia::ui::input2::Key::DOWN},
    {HID_USAGE_KEY_UP, fuchsia::ui::input2::Key::UP},
    {HID_USAGE_KEY_NUMLOCK, fuchsia::ui::input2::Key::NUM_LOCK},
    {HID_USAGE_KEY_KP_SLASH, fuchsia::ui::input2::Key::KEYPAD_SLASH},
    {HID_USAGE_KEY_KP_ASTERISK, fuchsia::ui::input2::Key::KEYPAD_ASTERISK},
    {HID_USAGE_KEY_KP_MINUS, fuchsia::ui::input2::Key::KEYPAD_MINUS},
    {HID_USAGE_KEY_KP_PLUS, fuchsia::ui::input2::Key::KEYPAD_PLUS},
    {HID_USAGE_KEY_KP_ENTER, fuchsia::ui::input2::Key::KEYPAD_ENTER},
    {HID_USAGE_KEY_KP_1, fuchsia::ui::input2::Key::KEYPAD_1},
    {HID_USAGE_KEY_KP_2, fuchsia::ui::input2::Key::KEYPAD_2},
    {HID_USAGE_KEY_KP_3, fuchsia::ui::input2::Key::KEYPAD_3},
    {HID_USAGE_KEY_KP_4, fuchsia::ui::input2::Key::KEYPAD_4},
    {HID_USAGE_KEY_KP_5, fuchsia::ui::input2::Key::KEYPAD_5},
    {HID_USAGE_KEY_KP_6, fuchsia::ui::input2::Key::KEYPAD_6},
    {HID_USAGE_KEY_KP_7, fuchsia::ui::input2::Key::KEYPAD_7},
    {HID_USAGE_KEY_KP_8, fuchsia::ui::input2::Key::KEYPAD_8},
    {HID_USAGE_KEY_KP_9, fuchsia::ui::input2::Key::KEYPAD_9},
    {HID_USAGE_KEY_KP_0, fuchsia::ui::input2::Key::KEYPAD_0},
    {HID_USAGE_KEY_KP_DOT, fuchsia::ui::input2::Key::KEYPAD_DOT},
    {HID_USAGE_KEY_NON_US_BACKSLASH, fuchsia::ui::input2::Key::NON_US_BACKSLASH},
    {HID_USAGE_KEY_LEFT_CTRL, fuchsia::ui::input2::Key::LEFT_CTRL},
    {HID_USAGE_KEY_LEFT_SHIFT, fuchsia::ui::input2::Key::LEFT_SHIFT},
    {HID_USAGE_KEY_LEFT_ALT, fuchsia::ui::input2::Key::LEFT_ALT},
    {HID_USAGE_KEY_LEFT_GUI, fuchsia::ui::input2::Key::LEFT_META},
    {HID_USAGE_KEY_RIGHT_CTRL, fuchsia::ui::input2::Key::RIGHT_CTRL},
    {HID_USAGE_KEY_RIGHT_SHIFT, fuchsia::ui::input2::Key::RIGHT_SHIFT},
    {HID_USAGE_KEY_RIGHT_ALT, fuchsia::ui::input2::Key::RIGHT_ALT},
    {HID_USAGE_KEY_RIGHT_GUI, fuchsia::ui::input2::Key::RIGHT_META},
    {HID_USAGE_KEY_VOL_DOWN, fuchsia::ui::input2::Key::MEDIA_VOLUME_DECREMENT},
    {HID_USAGE_KEY_VOL_UP, fuchsia::ui::input2::Key::MEDIA_VOLUME_INCREMENT},
};

std::optional<fuchsia::ui::input2::KeyEvent> into_key_event(
    const fuchsia::ui::input::KeyboardEvent& event) {
  fuchsia::ui::input2::KeyEvent key_event;

  hid::Usage hid_usage = hid::USAGE(hid::usage::Page::kKeyboardKeypad, event.hid_usage);
  if (auto key = hid_key_to_fuchsia_key(hid_usage)) {
    key_event.set_key(*key);
  } else {
    return {};
  }

  if (event.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED) {
    key_event.set_phase(fuchsia::ui::input2::KeyEventPhase::PRESSED);
  } else if (event.phase == fuchsia::ui::input::KeyboardEventPhase::RELEASED) {
    key_event.set_phase(fuchsia::ui::input2::KeyEventPhase::RELEASED);
  } else {
    return {};
  }

  if (event.modifiers == fuchsia::ui::input::kModifierNone) {
    return key_event;
  }

  fuchsia::ui::input2::Modifiers modifiers = {};

  if (event.modifiers & fuchsia::ui::input::kModifierLeftAlt) {
    modifiers |= fuchsia::ui::input2::Modifiers::ALT;
    modifiers |= fuchsia::ui::input2::Modifiers::LEFT_ALT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightAlt) {
    modifiers |= fuchsia::ui::input2::Modifiers::ALT;
    modifiers |= fuchsia::ui::input2::Modifiers::RIGHT_ALT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftShift) {
    modifiers |= fuchsia::ui::input2::Modifiers::SHIFT;
    modifiers |= fuchsia::ui::input2::Modifiers::LEFT_SHIFT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightShift) {
    modifiers |= fuchsia::ui::input2::Modifiers::SHIFT;
    modifiers |= fuchsia::ui::input2::Modifiers::RIGHT_SHIFT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftControl) {
    modifiers |= fuchsia::ui::input2::Modifiers::CONTROL;
    modifiers |= fuchsia::ui::input2::Modifiers::LEFT_CONTROL;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightControl) {
    modifiers |= fuchsia::ui::input2::Modifiers::CONTROL;
    modifiers |= fuchsia::ui::input2::Modifiers::RIGHT_CONTROL;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftSuper) {
    modifiers |= fuchsia::ui::input2::Modifiers::META;
    modifiers |= fuchsia::ui::input2::Modifiers::LEFT_META;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightSuper) {
    modifiers |= fuchsia::ui::input2::Modifiers::META;
    modifiers |= fuchsia::ui::input2::Modifiers::RIGHT_META;
  }
  key_event.set_modifiers(modifiers);
  return key_event;
}

std::optional<fuchsia::ui::input2::Key> hid_key_to_fuchsia_key(hid::Usage usage) {
  if (usage.page == hid::usage::Page::kKeyboardKeypad) {
    for (const auto& mapping : key_map) {
      if (std::get<0>(mapping) == usage.usage) {
        return std::get<1>(mapping);
      }
    }
  }
  return {};
}

std::optional<fuchsia::input::Key> hid_key_to_fuchsia_key3(hid::Usage usage) {
  if (usage.page == hid::usage::Page::kKeyboardKeypad) {
    auto code = (((uint8_t)(hid::usage::Page::kKeyboardKeypad)) & 0xFF) << 16 | (usage.usage);
    if (code == ((uint32_t)fuchsia::input::Key::KEYPAD_EQUALS) ||
        code == ((uint32_t)fuchsia::input::Key::MENU) ||
        (code >= ((uint32_t)fuchsia::input::Key::A) &&
         code <= ((uint32_t)fuchsia::input::Key::NON_US_BACKSLASH)) ||
        (code >= ((uint32_t)fuchsia::input::Key::LEFT_CTRL) &&
         (code <= ((uint32_t)fuchsia::input::Key::RIGHT_META)))) {
      return static_cast<fuchsia::input::Key>(code);
    } else {
      std::cout << "hid_key_to_fuchsia_key3 miss: " << std::hex << code << "\n";
      return {};
    }
  }
  return {};
}

std::optional<uint32_t> fuchsia_key_to_hid_key(fuchsia::ui::input2::Key key) {
  for (const auto& mapping : key_map) {
    if (std::get<1>(mapping) == key) {
      return std::get<0>(mapping);
    }
  }
  return {};
}

}  // namespace key_util
