// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/key_util.h"

namespace root_presenter {

constexpr std::pair<uint32_t, fuchsia::ui::input::Key> key_map[] = {
    {HID_USAGE_KEY_A, fuchsia::ui::input::Key::A},
    {HID_USAGE_KEY_B, fuchsia::ui::input::Key::B},
    {HID_USAGE_KEY_C, fuchsia::ui::input::Key::C},
    {HID_USAGE_KEY_D, fuchsia::ui::input::Key::D},
    {HID_USAGE_KEY_E, fuchsia::ui::input::Key::E},
    {HID_USAGE_KEY_F, fuchsia::ui::input::Key::F},
    {HID_USAGE_KEY_G, fuchsia::ui::input::Key::G},
    {HID_USAGE_KEY_H, fuchsia::ui::input::Key::H},
    {HID_USAGE_KEY_I, fuchsia::ui::input::Key::I},
    {HID_USAGE_KEY_J, fuchsia::ui::input::Key::J},
    {HID_USAGE_KEY_K, fuchsia::ui::input::Key::K},
    {HID_USAGE_KEY_L, fuchsia::ui::input::Key::L},
    {HID_USAGE_KEY_M, fuchsia::ui::input::Key::M},
    {HID_USAGE_KEY_N, fuchsia::ui::input::Key::N},
    {HID_USAGE_KEY_O, fuchsia::ui::input::Key::O},
    {HID_USAGE_KEY_P, fuchsia::ui::input::Key::P},
    {HID_USAGE_KEY_Q, fuchsia::ui::input::Key::Q},
    {HID_USAGE_KEY_R, fuchsia::ui::input::Key::R},
    {HID_USAGE_KEY_S, fuchsia::ui::input::Key::S},
    {HID_USAGE_KEY_T, fuchsia::ui::input::Key::T},
    {HID_USAGE_KEY_U, fuchsia::ui::input::Key::U},
    {HID_USAGE_KEY_V, fuchsia::ui::input::Key::V},
    {HID_USAGE_KEY_W, fuchsia::ui::input::Key::W},
    {HID_USAGE_KEY_X, fuchsia::ui::input::Key::X},
    {HID_USAGE_KEY_Y, fuchsia::ui::input::Key::Y},
    {HID_USAGE_KEY_Z, fuchsia::ui::input::Key::Z},
    {HID_USAGE_KEY_1, fuchsia::ui::input::Key::KEY_1},
    {HID_USAGE_KEY_2, fuchsia::ui::input::Key::KEY_2},
    {HID_USAGE_KEY_3, fuchsia::ui::input::Key::KEY_3},
    {HID_USAGE_KEY_4, fuchsia::ui::input::Key::KEY_4},
    {HID_USAGE_KEY_5, fuchsia::ui::input::Key::KEY_5},
    {HID_USAGE_KEY_6, fuchsia::ui::input::Key::KEY_6},
    {HID_USAGE_KEY_7, fuchsia::ui::input::Key::KEY_7},
    {HID_USAGE_KEY_8, fuchsia::ui::input::Key::KEY_8},
    {HID_USAGE_KEY_9, fuchsia::ui::input::Key::KEY_9},
    {HID_USAGE_KEY_0, fuchsia::ui::input::Key::KEY_0},
    {HID_USAGE_KEY_ENTER, fuchsia::ui::input::Key::ENTER},
    {HID_USAGE_KEY_ESC, fuchsia::ui::input::Key::ESCAPE},
    {HID_USAGE_KEY_BACKSPACE, fuchsia::ui::input::Key::BACKSPACE},
    {HID_USAGE_KEY_TAB, fuchsia::ui::input::Key::TAB},
    {HID_USAGE_KEY_SPACE, fuchsia::ui::input::Key::SPACE},
    {HID_USAGE_KEY_MINUS, fuchsia::ui::input::Key::MINUS},
    {HID_USAGE_KEY_EQUAL, fuchsia::ui::input::Key::EQUALS},
    {HID_USAGE_KEY_LEFTBRACE, fuchsia::ui::input::Key::LEFT_BRACE},
    {HID_USAGE_KEY_RIGHTBRACE, fuchsia::ui::input::Key::RIGHT_BRACE},
    {HID_USAGE_KEY_BACKSLASH, fuchsia::ui::input::Key::BACKSLASH},
    {HID_USAGE_KEY_NON_US_OCTOTHORPE, fuchsia::ui::input::Key::NON_US_HASH},
    {HID_USAGE_KEY_SEMICOLON, fuchsia::ui::input::Key::SEMICOLON},
    {HID_USAGE_KEY_APOSTROPHE, fuchsia::ui::input::Key::APOSTROPHE},
    {HID_USAGE_KEY_GRAVE, fuchsia::ui::input::Key::GRAVE_ACCENT},
    {HID_USAGE_KEY_COMMA, fuchsia::ui::input::Key::COMMA},
    {HID_USAGE_KEY_DOT, fuchsia::ui::input::Key::DOT},
    {HID_USAGE_KEY_SLASH, fuchsia::ui::input::Key::SLASH},
    {HID_USAGE_KEY_CAPSLOCK, fuchsia::ui::input::Key::CAPS_LOCK},
    {HID_USAGE_KEY_F1, fuchsia::ui::input::Key::F1},
    {HID_USAGE_KEY_F2, fuchsia::ui::input::Key::F2},
    {HID_USAGE_KEY_F3, fuchsia::ui::input::Key::F3},
    {HID_USAGE_KEY_F4, fuchsia::ui::input::Key::F4},
    {HID_USAGE_KEY_F5, fuchsia::ui::input::Key::F5},
    {HID_USAGE_KEY_F6, fuchsia::ui::input::Key::F6},
    {HID_USAGE_KEY_F7, fuchsia::ui::input::Key::F7},
    {HID_USAGE_KEY_F8, fuchsia::ui::input::Key::F8},
    {HID_USAGE_KEY_F9, fuchsia::ui::input::Key::F9},
    {HID_USAGE_KEY_F10, fuchsia::ui::input::Key::F10},
    {HID_USAGE_KEY_F11, fuchsia::ui::input::Key::F11},
    {HID_USAGE_KEY_F12, fuchsia::ui::input::Key::F12},
    {HID_USAGE_KEY_PRINTSCREEN, fuchsia::ui::input::Key::PRINT_SCREEN},
    {HID_USAGE_KEY_SCROLLLOCK, fuchsia::ui::input::Key::SCROLL_LOCK},
    {HID_USAGE_KEY_PAUSE, fuchsia::ui::input::Key::PAUSE},
    {HID_USAGE_KEY_INSERT, fuchsia::ui::input::Key::INSERT},
    {HID_USAGE_KEY_HOME, fuchsia::ui::input::Key::HOME},
    {HID_USAGE_KEY_PAGEUP, fuchsia::ui::input::Key::PAGE_UP},
    {HID_USAGE_KEY_DELETE, fuchsia::ui::input::Key::DELETE},
    {HID_USAGE_KEY_END, fuchsia::ui::input::Key::END},
    {HID_USAGE_KEY_PAGEDOWN, fuchsia::ui::input::Key::PAGE_DOWN},
    {HID_USAGE_KEY_RIGHT, fuchsia::ui::input::Key::RIGHT},
    {HID_USAGE_KEY_LEFT, fuchsia::ui::input::Key::LEFT},
    {HID_USAGE_KEY_DOWN, fuchsia::ui::input::Key::DOWN},
    {HID_USAGE_KEY_UP, fuchsia::ui::input::Key::UP},
    {HID_USAGE_KEY_NUMLOCK, fuchsia::ui::input::Key::NUM_LOCK},
    {HID_USAGE_KEY_KP_SLASH, fuchsia::ui::input::Key::KEYPAD_SLASH},
    {HID_USAGE_KEY_KP_ASTERISK, fuchsia::ui::input::Key::KEYPAD_ASTERISK},
    {HID_USAGE_KEY_KP_MINUS, fuchsia::ui::input::Key::KEYPAD_MINUS},
    {HID_USAGE_KEY_KP_PLUS, fuchsia::ui::input::Key::KEYPAD_PLUS},
    {HID_USAGE_KEY_KP_ENTER, fuchsia::ui::input::Key::KEYPAD_ENTER},
    {HID_USAGE_KEY_KP_1, fuchsia::ui::input::Key::KEYPAD_1},
    {HID_USAGE_KEY_KP_2, fuchsia::ui::input::Key::KEYPAD_2},
    {HID_USAGE_KEY_KP_3, fuchsia::ui::input::Key::KEYPAD_3},
    {HID_USAGE_KEY_KP_4, fuchsia::ui::input::Key::KEYPAD_4},
    {HID_USAGE_KEY_KP_5, fuchsia::ui::input::Key::KEYPAD_5},
    {HID_USAGE_KEY_KP_6, fuchsia::ui::input::Key::KEYPAD_6},
    {HID_USAGE_KEY_KP_7, fuchsia::ui::input::Key::KEYPAD_7},
    {HID_USAGE_KEY_KP_8, fuchsia::ui::input::Key::KEYPAD_8},
    {HID_USAGE_KEY_KP_9, fuchsia::ui::input::Key::KEYPAD_9},
    {HID_USAGE_KEY_KP_0, fuchsia::ui::input::Key::KEYPAD_0},
    {HID_USAGE_KEY_KP_DOT, fuchsia::ui::input::Key::KEYPAD_DOT},
    {HID_USAGE_KEY_NON_US_BACKSLASH, fuchsia::ui::input::Key::NON_US_BACKSLASH},
    {HID_USAGE_KEY_LEFT_CTRL, fuchsia::ui::input::Key::LEFT_CTRL},
    {HID_USAGE_KEY_LEFT_SHIFT, fuchsia::ui::input::Key::LEFT_SHIFT},
    {HID_USAGE_KEY_LEFT_ALT, fuchsia::ui::input::Key::LEFT_ALT},
    {HID_USAGE_KEY_LEFT_GUI, fuchsia::ui::input::Key::LEFT_META},
    {HID_USAGE_KEY_RIGHT_CTRL, fuchsia::ui::input::Key::RIGHT_CTRL},
    {HID_USAGE_KEY_RIGHT_SHIFT, fuchsia::ui::input::Key::RIGHT_SHIFT},
    {HID_USAGE_KEY_RIGHT_ALT, fuchsia::ui::input::Key::RIGHT_ALT},
    {HID_USAGE_KEY_RIGHT_GUI, fuchsia::ui::input::Key::RIGHT_META},
    {HID_USAGE_KEY_VOL_DOWN, fuchsia::ui::input::Key::MEDIA_VOLUME_DECREMENT},
    {HID_USAGE_KEY_VOL_UP, fuchsia::ui::input::Key::MEDIA_VOLUME_INCREMENT},
};

std::optional<fuchsia::ui::input::KeyEvent> into_key_event(
    const fuchsia::ui::input::KeyboardEvent& event) {
  fuchsia::ui::input::KeyEvent key_event;

  if (auto key = into_key(event.hid_usage)) {
    key_event.set_key(*key);
  } else {
    return {};
  }

  if (event.phase == fuchsia::ui::input::KeyboardEventPhase::PRESSED) {
    key_event.set_phase(fuchsia::ui::input::KeyEventPhase::PRESSED);
  } else if (event.phase == fuchsia::ui::input::KeyboardEventPhase::RELEASED) {
    key_event.set_phase(fuchsia::ui::input::KeyEventPhase::RELEASED);
  } else {
    return {};
  }

  if (event.modifiers == fuchsia::ui::input::kModifierNone) {
    return key_event;
  }

  fuchsia::ui::input::Modifiers modifiers = {};

  if (event.modifiers & fuchsia::ui::input::kModifierLeftAlt) {
    modifiers |= fuchsia::ui::input::Modifiers::ALT;
    modifiers |= fuchsia::ui::input::Modifiers::LEFT_ALT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightAlt) {
    modifiers |= fuchsia::ui::input::Modifiers::ALT;
    modifiers |= fuchsia::ui::input::Modifiers::RIGHT_ALT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftShift) {
    modifiers |= fuchsia::ui::input::Modifiers::SHIFT;
    modifiers |= fuchsia::ui::input::Modifiers::LEFT_SHIFT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightShift) {
    modifiers |= fuchsia::ui::input::Modifiers::SHIFT;
    modifiers |= fuchsia::ui::input::Modifiers::RIGHT_SHIFT;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftControl) {
    modifiers |= fuchsia::ui::input::Modifiers::CONTROL;
    modifiers |= fuchsia::ui::input::Modifiers::LEFT_CONTROL;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightControl) {
    modifiers |= fuchsia::ui::input::Modifiers::CONTROL;
    modifiers |= fuchsia::ui::input::Modifiers::RIGHT_CONTROL;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierLeftSuper) {
    modifiers |= fuchsia::ui::input::Modifiers::META;
    modifiers |= fuchsia::ui::input::Modifiers::LEFT_META;
  }
  if (event.modifiers & fuchsia::ui::input::kModifierRightSuper) {
    modifiers |= fuchsia::ui::input::Modifiers::META;
    modifiers |= fuchsia::ui::input::Modifiers::RIGHT_META;
  }
  key_event.set_modifiers(modifiers);
  return key_event;
}

std::optional<fuchsia::ui::input::Key> into_key(uint32_t hid) {
  for (const auto& mapping : key_map) {
    if (std::get<0>(mapping) == hid) {
      return std::get<1>(mapping);
    }
  }
  return {};
}

}  // namespace root_presenter
