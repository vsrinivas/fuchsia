// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/key_util/key_util.h"

#include "gtest/gtest.h"
#include "hid-parser/usages.h"
#include "hid/usages.h"

namespace root_presenter {
namespace {

TEST(InputUtil, IntoKeyEvent) {
  fuchsia::ui::input::KeyboardEvent kbd = {};
  kbd.phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  kbd.hid_usage = HID_USAGE_KEY_A;
  kbd.modifiers = fuchsia::ui::input::kModifierLeftShift;

  std::optional<fuchsia::ui::input2::KeyEvent> key = key_util::into_key_event(kbd);

  EXPECT_TRUE(key);
  EXPECT_EQ(key->phase(), fuchsia::ui::input2::KeyEventPhase::PRESSED);
  EXPECT_EQ(key->key(), fuchsia::ui::input2::Key::A);
  EXPECT_EQ(key->modifiers(),
            fuchsia::ui::input2::Modifiers::SHIFT | fuchsia::ui::input2::Modifiers::LEFT_SHIFT);
}

TEST(InputUtil, IntoKeyEventUnknown) {
  fuchsia::ui::input::KeyboardEvent kbd = {};
  kbd.hid_usage = HID_USAGE_KEY_ERROR_ROLLOVER;

  std::optional<fuchsia::ui::input2::KeyEvent> key = key_util::into_key_event(kbd);

  EXPECT_FALSE(key);
}

}  // namespace
}  // namespace root_presenter
