// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "keyboard.h"

#include <array>

#include <hid/hid.h>
#include <hid/usages.h>
#include <zxtest/zxtest.h>

#include "keyboard-vt100.h"

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
void expect_no_keypress() { EXPECT_FALSE(g_got_keypress); }

class KeyboardInputHelper {
 public:
  KeyboardInputHelper() { EXPECT_OK(vc_input_create(&vi_, keypress_handler, -1)); }

  ~KeyboardInputHelper() {}

  void WriteReportBuf() { vc_input_process(vi_, report_buf_.data()); }

  // Byte 0 contains one bit per modifier key.
  void set_modifiers_byte(uint8_t value) { report_buf_[0] = value; }
  // Bytes 2+ contain USB HID key codes.
  void set_first_keycode(uint8_t value) { report_buf_[2] = value; }
  // Rollover errors are equal to all values being set to ROLLOVER_ERROR.
  void set_rollover_error() {
    report_buf_[0] = HID_USAGE_KEY_ERROR_ROLLOVER;
    report_buf_[1] = 0;
    for (auto i = 2; i < 8; i++) {
      report_buf_[i] = HID_USAGE_KEY_ERROR_ROLLOVER;
    }
  }
  void unset_rollover_error() {
    for (auto i = 0; i < 8; i++) {
      report_buf_[i] = 0;
    }
  }

 private:
  // USB HID key state buffer.
  std::array<uint8_t, 8> report_buf_ = {};

  vc_input_t* vi_;
};

TEST(GfxConsoleKeyboardTests, KeyboardInputThread) {
  KeyboardInputHelper helper;

  // Test pressing keys without any modifiers.
  helper.set_first_keycode(HID_USAGE_KEY_M);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_M, 0, 'm'));

  helper.set_first_keycode(HID_USAGE_KEY_6);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_6, 0, '6'));

  // Simulate a rollover event appearing and disappearing — no keypress should be registered
  helper.set_rollover_error();
  helper.WriteReportBuf();
  expect_no_keypress();

  // Send the keycode that was pressed in the previous test before the
  // rollover happened. No new keypress should register.
  helper.unset_rollover_error();
  helper.set_first_keycode(HID_USAGE_KEY_6);
  helper.WriteReportBuf();
  expect_no_keypress();

  // Press a modifier (but no other keys).
  helper.set_first_keycode(0);   // Unset the earlier key
  helper.set_modifiers_byte(2);  // Left Shift key
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0'));

  // Test keys with modifiers pressed.
  // Test Shift-N.
  helper.set_first_keycode(HID_USAGE_KEY_N);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_N, MOD_LSHIFT, 'N'));

  // Test Shift-8.
  helper.set_first_keycode(HID_USAGE_KEY_8);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_8, MOD_LSHIFT, '*'));

  // Test Ctrl modifier.  First send a separate report_buf event to
  // report unsetting the Shift key state, to account for a quirk of the
  // current implementation.
  helper.set_modifiers_byte(0);
  helper.WriteReportBuf();
  helper.set_modifiers_byte(1);  // Left Ctrl key
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_CTRL, MOD_LCTRL, '\0'));

  // Test Ctrl-J.
  helper.set_first_keycode(HID_USAGE_KEY_J);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_J, MOD_LCTRL, 10));

  // Test Ctrl-1.  The Ctrl modifier should be ignored in this case so
  // that we just get '1'.
  helper.set_first_keycode(HID_USAGE_KEY_1);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LCTRL, '1'));

  // Try Shift and Ctrl together.
  helper.set_first_keycode(0);
  helper.set_modifiers_byte(1 | 2);  // Left Shift and Left Ctrl keys
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT | MOD_LCTRL, '\0'));

  // Test Shift-Ctrl-J.  This should be equivalent to Ctrl-J.
  helper.set_first_keycode(HID_USAGE_KEY_J);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_J, MOD_LSHIFT | MOD_LCTRL, 10));

  // Test Shift-Ctrl-1.  This should be equivalent to Shift-1.
  helper.set_first_keycode(HID_USAGE_KEY_1);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_LCTRL, '!'));
}

TEST(GfxConsoleKeyboardTests, CapsLock) {
  KeyboardInputHelper helper;

  helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_CAPSLOCK, '\0'));

  // Test that letters are capitalized.
  helper.set_first_keycode(HID_USAGE_KEY_M);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_CAPSLOCK, 'M'));

  // Non-letter characters should not be affected.  This isn't Shift Lock.
  helper.set_first_keycode(HID_USAGE_KEY_1);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_CAPSLOCK, '1'));

  // Test unsetting Caps Lock.
  helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, 0, '\0'));

  helper.set_first_keycode(HID_USAGE_KEY_M);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_M, 0, 'm'));
}

TEST(GfxConsoleKeyboardTests, CapsLockWithShift) {
  KeyboardInputHelper helper;

  helper.set_modifiers_byte(2);  // Left Shift key
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0'));
  helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(
      expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT | MOD_CAPSLOCK, '\0'));

  // Shift should undo the effect of Caps Lock for letters.
  helper.set_first_keycode(HID_USAGE_KEY_M);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT | MOD_CAPSLOCK, 'm'));

  helper.set_first_keycode(HID_USAGE_KEY_1);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_CAPSLOCK, '!'));

  // Test unsetting Caps Lock.
  helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT, '\0'));

  helper.set_first_keycode(HID_USAGE_KEY_M);
  helper.WriteReportBuf();
  ASSERT_NO_FATAL_FAILURES(expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT, 'M'));
}

}  // namespace
