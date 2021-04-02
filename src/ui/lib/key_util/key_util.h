// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_
#define SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_

#include <fuchsia/ui/input/cpp/fidl.h>

#include <optional>

#include <hid-parser/parser.h>

namespace key_util {

// Function to convert Fuchsia Key to a HID Usage.
// The HID usage will be from page 0x7 (Keyboard/Keypad).
uint32_t fuchsia_key3_to_hid_key(fuchsia::input::Key);

std::optional<fuchsia::input::Key> hid_key_to_fuchsia_key3(hid::Usage usage);

}  // namespace key_util

#endif  // SRC_UI_LIB_KEY_UTIL_KEY_UTIL_H_
