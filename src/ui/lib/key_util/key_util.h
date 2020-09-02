// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_
#define SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>
#include <fuchsia/ui/input2/cpp/fidl.h>

#include <optional>

#include <hid-parser/parser.h>

namespace key_util {

// Utility function to create Key event from Keyboard event.
std::optional<fuchsia::ui::input2::KeyEvent> into_key_event(
    const fuchsia::ui::input::KeyboardEvent& event);

// Function to convert HID usage to a Fuchsia Key.
// The HID usage must be from page 0x7 (Keyboard/Keypad).
std::optional<fuchsia::ui::input2::Key> hid_key_to_fuchsia_key(hid::Usage usage);

// Function to convert Fuchsia Key to a HID Usage.
// The HID usage will be from page 0x7 (Keyboard/Keypad).
std::optional<uint32_t> fuchsia_key_to_hid_key(fuchsia::ui::input2::Key);

std::optional<fuchsia::input::Key> hid_key_to_fuchsia_key3(hid::Usage usage);

}  // namespace key_util

#endif  // SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_
