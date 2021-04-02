// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/key_util/key_util.h"

#include "hid-parser/usages.h"
#include "hid/usages.h"

namespace key_util {

uint32_t fuchsia_key3_to_hid_key(fuchsia::input::Key key) { return static_cast<uint32_t>(key); }

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
    }
  }
  return {};
}

}  // namespace key_util
