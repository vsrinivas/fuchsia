// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/input/inverse_keymap.h"

namespace input {

InverseKeymap InvertKeymap(const keychar_t keymap[]) {
  InverseKeymap inverse;

  for (uint32_t usage = 0; usage < KEYMAP_SIZE; ++usage) {
    const keychar_t& mapping = keymap[usage];
    if (mapping.c) {
      Keystroke& keystroke = inverse[mapping.c];
      keystroke.usage = usage;
      keystroke.shift = mapping.c == mapping.shift_c
                            ? Keystroke::Shift::kDontCare
                            : Keystroke::Shift::kNo;
    }

    if (mapping.shift_c && mapping.shift_c != mapping.c) {
      inverse[mapping.shift_c] = {usage, Keystroke::Shift::kYes};
    }
  }

  return inverse;
}

std::pair<KeySequence, bool> DeriveKeySequence(
    const InverseKeymap& inverse_keymap, const std::string& text) {
  uint32_t last_key = 0;
  bool shift = false;
  KeySequence key_sequence;
  key_sequence.reserve(text.length() + 1);

  for (auto next = text.begin(); next != text.end();) {
    auto report = fuchsia::ui::input::KeyboardReport::New();
    report->pressed_keys.resize(0);

    const auto it = inverse_keymap.find(*next);
    if (it == inverse_keymap.end()) {
      return {KeySequence(), false};
    }

    const Keystroke& keystroke = it->second;

    if (keystroke.shift == Keystroke::Shift::kYes && !shift) {
      shift = true;
      last_key = 0;
    } else if (keystroke.shift == Keystroke::Shift::kNo && shift) {
      shift = false;
      last_key = 0;
    } else {
      // If the shift key changes, we need to send its transition separately to
      // guarantee clients handle it as expected, so only process other keys if
      // there's no shift transition.

      if (last_key == keystroke.usage) {
        // If the key is already down, clear it first.
        // (We can go ahead and send the next shift-key state below though).
        last_key = 0;
      } else {
        report->pressed_keys.push_back(keystroke.usage);
        last_key = keystroke.usage;
        ++next;
      }
    }

    // HID_USAGE_KEY_LEFT_SHIFT > all symbolic keys, and I believe the real impl
    // always sends keys in ascending order, so do this last.
    if (shift) {
      report->pressed_keys.push_back(HID_USAGE_KEY_LEFT_SHIFT);
    }

    key_sequence.emplace_back(std::move(report));
  }

  // Make sure we end on an empty report.
  if (key_sequence.size() > 0) {
    auto report = fuchsia::ui::input::KeyboardReport::New();
    report->pressed_keys.resize(0);
    key_sequence.emplace_back(std::move(report));
  }

  return {std::move(key_sequence), true};
}

}  // namespace input
