// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "keyboard.h"

#include <array>

#include <hid/hid.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "keyboard-vt100.h"
#include "src/ui/lib/hid-input-report/fidl.h"
#include "src/ui/lib/hid-input-report/keyboard.h"
#include "src/ui/lib/key_util/key_util.h"

// Global port needed for IPC calls.
port_t port;

namespace {

// State reported to keypress_handler().
uint8_t g_keycode;
int g_modifiers;
bool g_got_keypress = false;

void keypress_handler(uint8_t keycode, int modifiers) {
  g_keycode = keycode;
  g_modifiers = modifiers;
  g_got_keypress = true;
}

void expect_keypress(uint8_t expected_keycode, int expected_modifiers, uint8_t expected_char) {
  EXPECT_EQ(g_got_keypress, true);
  g_got_keypress = false;

  EXPECT_EQ(g_keycode, expected_keycode);
  EXPECT_EQ(g_modifiers, expected_modifiers);

  std::array<char, 4> output = {};
  uint32_t length =
      hid_key_to_vt100_code(g_keycode, g_modifiers, qwerty_map, output.data(), output.size());
  if (expected_char == 0) {
    EXPECT_EQ(length, 0);
  } else {
    ASSERT_EQ(length, 1);
    EXPECT_EQ(output[0], expected_char);
  }
}

class KeyboardInputHelper {
 public:
  KeyboardInputHelper() : keyboard_(keypress_handler, true) {}

  ~KeyboardInputHelper() {}

  void WriteReportBuf(std::vector<uint32_t> keys) {
    hid_input_report::KeyboardInputReport keyboard_report;
    size_t i = 0;
    for (auto& key : keys) {
      fuchsia::ui::input2::Key fidl_key =
          *key_util::hid_key_to_fuchsia_key(hid::USAGE(hid::usage::Page::kKeyboardKeypad, key));
      keyboard_report.pressed_keys[i++] = static_cast<llcpp::fuchsia::ui::input2::Key>(fidl_key);
    }
    keyboard_report.num_pressed_keys = i;
    hid_input_report::InputReport report;
    report.report = keyboard_report;

    hid_input_report::FidlInputReport fidl;
    hid_input_report::SetFidlInputReport(report, &fidl);
    keyboard_.ProcessInput(fidl.builder.view());
  }

  // Byte 0 contains one bit per modifier key.
  void set_modifiers_byte(uint8_t value) {}
  // Bytes 2+ contain USB HID key codes.
  void set_first_keycode(uint8_t value) {}

 private:
  Keyboard keyboard_;
};

TEST(GfxConsoleKeyboardTests, KeyboardInputThread) {
  KeyboardInputHelper helper;
  std::vector<uint32_t> keypresses;

  // Test pressing keys without any modifiers.
  keypresses = {HID_USAGE_KEY_M};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_M, 0, 'm'));

  keypresses = {HID_USAGE_KEY_6};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_6, 0, '6'));

  // Press a modifier (but no other keys).
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0'));

  // Test keys with modifiers pressed.
  // Test Shift-N.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_N};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_N, MOD_LSHIFT, 'N'));

  // Test Shift-8.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_8};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_8, MOD_LSHIFT, '*'));

  // Test Ctrl modifier.
  keypresses = {HID_USAGE_KEY_LEFT_CTRL};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_CTRL, MOD_LCTRL, '\0'));

  // Test Ctrl-J.
  keypresses = {HID_USAGE_KEY_J, HID_USAGE_KEY_LEFT_CTRL};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_J, MOD_LCTRL, 10));

  // Test Ctrl-1.  The Ctrl modifier should be ignored in this case so
  // that we just get '1'.
  keypresses = {HID_USAGE_KEY_1, HID_USAGE_KEY_LEFT_CTRL};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LCTRL, '1'));

  // Try Shift and Ctrl together.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_LEFT_CTRL};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT | MOD_LCTRL, '\0'));

  // Test Shift-Ctrl-J.  This should be equivalent to Ctrl-J.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_LEFT_CTRL, HID_USAGE_KEY_J};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_J, MOD_LSHIFT | MOD_LCTRL, 10));

  // Test Shift-Ctrl-1.  This should be equivalent to Shift-1.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_LEFT_CTRL, HID_USAGE_KEY_1};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_LCTRL, '!'));
}

TEST(GfxConsoleKeyboardTests, CapsLock) {
  KeyboardInputHelper helper;
  std::vector<uint32_t> keypresses;

  keypresses = {HID_USAGE_KEY_CAPSLOCK};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_CAPSLOCK, '\0'));

  // Test that letters are capitalized.
  keypresses = {HID_USAGE_KEY_M};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_CAPSLOCK, 'M'));

  // Non-letter characters should not be affected.  This isn't Shift Lock.
  keypresses = {HID_USAGE_KEY_1};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_CAPSLOCK, '1'));

  // Test unsetting Caps Lock.
  keypresses = {HID_USAGE_KEY_CAPSLOCK};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, 0, '\0'));

  keypresses = {HID_USAGE_KEY_M};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_M, 0, 'm'));
}

TEST(GfxConsoleKeyboardTests, CapsLockWithShift) {
  KeyboardInputHelper helper;
  std::vector<uint32_t> keypresses;

  keypresses = {HID_USAGE_KEY_LEFT_SHIFT};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0'));

  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_CAPSLOCK};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT | MOD_CAPSLOCK, '\0'));

  // Shift should undo the effect of Caps Lock for letters.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_M};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT | MOD_CAPSLOCK, 'm'));

  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_1};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_CAPSLOCK, '!'));

  // Test unsetting Caps Lock.
  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_CAPSLOCK};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT, '\0'));

  keypresses = {HID_USAGE_KEY_LEFT_SHIFT, HID_USAGE_KEY_M};
  helper.WriteReportBuf(keypresses);
  ASSERT_NO_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT, 'M'));
}

}  // namespace
