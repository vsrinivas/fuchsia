// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/key_util.h"
#include "gtest/gtest.h"

namespace root_presenter {
namespace {

TEST(InputUtil, IntoKeyEvent) {
  fuchsia::ui::input::KeyboardEvent kbd = {};
  kbd.phase = fuchsia::ui::input::KeyboardEventPhase::PRESSED;
  kbd.hid_usage = HID_USAGE_KEY_A;
  kbd.modifiers = fuchsia::ui::input::kModifierLeftShift;

  std::optional<fuchsia::ui::input::KeyEvent> key = into_key_event(kbd);

  EXPECT_TRUE(key);
  EXPECT_EQ(key->phase(), fuchsia::ui::input::KeyEventPhase::PRESSED);
  EXPECT_EQ(key->key(), fuchsia::ui::input::Key::A);
  EXPECT_EQ(key->modifiers(),
            fuchsia::ui::input::Modifiers::SHIFT | fuchsia::ui::input::Modifiers::LEFT_SHIFT);
}

TEST(InputUtil, IntoKeyEventUnknown) {
  fuchsia::ui::input::KeyboardEvent kbd = {};
  kbd.hid_usage = HID_USAGE_KEY_ERROR_ROLLOVER;

  std::optional<fuchsia::ui::input::KeyEvent> key = into_key_event(kbd);

  EXPECT_FALSE(key);
}

}  // namespace
}  // namespace root_presenter
