// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/hid.h>
#include <hid/usages.h>
#include <mxtl/auto_lock.h>
#include <threads.h>
#include <unistd.h>
#include <unittest/unittest.h>

#include "keyboard-vt100.h"
#include "keyboard.h"

namespace {

// State reported to keypress_handler().
uint8_t g_keycode;
int g_modifiers;
bool g_got_keypress = false;
mtx_t g_mutex = MTX_INIT;
cnd_t g_cond = CND_INIT;

void keypress_handler(uint8_t keycode, int modifiers) {
    mxtl::AutoLock lock(&g_mutex);

    // Overwrite any existing key, in case autorepeat kicked in.
    g_keycode = keycode;
    g_modifiers = modifiers;
    g_got_keypress = true;
    cnd_signal(&g_cond);
}

void expect_keypress(uint8_t expected_keycode, int expected_modifiers,
                     uint8_t expected_char) {
    mxtl::AutoLock lock(&g_mutex);

    // Wait for event.
    while (!g_got_keypress)
        cnd_wait(&g_cond, &g_mutex);
    g_got_keypress = false;

    EXPECT_EQ(g_keycode, expected_keycode, "");
    EXPECT_EQ(g_modifiers, expected_modifiers, "");

    char output[4] = {};
    uint32_t length = hid_key_to_vt100_code(
        g_keycode, g_modifiers, qwerty_map, output, sizeof(output));
    if (expected_char == 0) {
        EXPECT_EQ(length, 0, "");
    } else {
        EXPECT_EQ(length, 1, "");
        EXPECT_EQ(output[0], expected_char, "");
    }
}

class KeyboardInputHelper {
public:
    KeyboardInputHelper() {
        int rc = pipe(pipe_fds_);
        EXPECT_EQ(rc, 0, "");

        auto* args = new vc_input_thread_args;
        args->fd = pipe_fds_[1];
        args->keypress_handler = keypress_handler;
        int ret = thrd_create_with_name(&thread_, vc_input_thread, args,
                                        "input");
        EXPECT_EQ(ret, thrd_success, "");
    }

    ~KeyboardInputHelper() {
        int ret = close(pipe_fds_[0]);
        EXPECT_EQ(ret, 0, "");

        // Test that the keyboard input thread exits properly after it
        // reads EOF.
        EXPECT_EQ(thrd_join(thread_, &ret), thrd_success, "");
    }

    void WriteReportBuf() {
        EXPECT_EQ(write(pipe_fds_[0], report_buf_, sizeof(report_buf_)),
                  sizeof(report_buf_), "");
    }

    // Byte 0 contains one bit per modifier key.
    void set_modifiers_byte(uint8_t value) { report_buf_[0] = value; }
    // Bytes 2+ contain USB HID key codes.
    void set_first_keycode(uint8_t value) { report_buf_[2] = value; }

private:
    int pipe_fds_[2];
    thrd_t thread_;
    // USB HID key state buffer.
    uint8_t report_buf_[8] = {};
};

bool test_keyboard_input_thread() {
    BEGIN_TEST;

    KeyboardInputHelper helper;

    // Test pressing keys without any modifiers.
    helper.set_first_keycode(HID_USAGE_KEY_M);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_M, 0, 'm');

    // Test autorepeat: After some delay, the same key should be reported
    // again.
    expect_keypress(HID_USAGE_KEY_M, 0, 'm');

    helper.set_first_keycode(HID_USAGE_KEY_6);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_6, 0, '6');

    // Press a modifier (but no other keys).
    helper.set_first_keycode(0); // Unset the earlier key
    helper.set_modifiers_byte(2); // Left Shift key
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0');

    // Test keys with modifiers pressed.
    // Test Shift-N.
    helper.set_first_keycode(HID_USAGE_KEY_N);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_N, MOD_LSHIFT, 'N');

    // Test Shift-8.
    helper.set_first_keycode(HID_USAGE_KEY_8);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_8, MOD_LSHIFT, '*');

    // Test Ctrl modifier.  First send a separate report_buf event to
    // report unsetting the Shift key state, to account for a quirk of the
    // current implementation.
    helper.set_modifiers_byte(0);
    helper.WriteReportBuf();
    helper.set_modifiers_byte(1); // Left Ctrl key
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_LEFT_CTRL, MOD_LCTRL, '\0');

    // Test Ctrl-J.
    helper.set_first_keycode(HID_USAGE_KEY_J);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_J, MOD_LCTRL, 10);

    // Test Ctrl-1.  The Ctrl modifier should be ignored in this case so
    // that we just get '1'.
    helper.set_first_keycode(HID_USAGE_KEY_1);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_1, MOD_LCTRL, '1');

    // Try Shift and Ctrl together.
    helper.set_first_keycode(0);
    helper.set_modifiers_byte(1 | 2); // Left Shift and Left Ctrl keys
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT | MOD_LCTRL, '\0');

    // Test Shift-Ctrl-J.  This should be equivalent to Ctrl-J.
    helper.set_first_keycode(HID_USAGE_KEY_J);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_J, MOD_LSHIFT | MOD_LCTRL, 10);

    // Test Shift-Ctrl-1.  This should be equivalent to Shift-1.
    helper.set_first_keycode(HID_USAGE_KEY_1);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_LCTRL, '!');

    END_TEST;
}

bool test_caps_lock() {
    BEGIN_TEST;

    KeyboardInputHelper helper;

    helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_CAPSLOCK, '\0');

    // Test that letters are capitalized.
    helper.set_first_keycode(HID_USAGE_KEY_M);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_M, MOD_CAPSLOCK, 'M');

    // Non-letter characters should not be affected.  This isn't Shift Lock.
    helper.set_first_keycode(HID_USAGE_KEY_1);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_1, MOD_CAPSLOCK, '1');

    // Test unsetting Caps Lock.
    helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_CAPSLOCK, 0, '\0');

    helper.set_first_keycode(HID_USAGE_KEY_M);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_M, 0, 'm');

    END_TEST;
}

bool test_caps_lock_with_shift() {
    BEGIN_TEST;

    KeyboardInputHelper helper;

    helper.set_modifiers_byte(2); // Left Shift key
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0');
    helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT | MOD_CAPSLOCK, '\0');

    // Shift should undo the effect of Caps Lock for letters.
    helper.set_first_keycode(HID_USAGE_KEY_M);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT | MOD_CAPSLOCK, 'm');

    helper.set_first_keycode(HID_USAGE_KEY_1);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_1, MOD_LSHIFT | MOD_CAPSLOCK, '!');

    // Test unsetting Caps Lock.
    helper.set_first_keycode(HID_USAGE_KEY_CAPSLOCK);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_CAPSLOCK, MOD_LSHIFT, '\0');

    helper.set_first_keycode(HID_USAGE_KEY_M);
    helper.WriteReportBuf();
    expect_keypress(HID_USAGE_KEY_M, MOD_LSHIFT, 'M');

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_keyboard_tests)
RUN_TEST(test_keyboard_input_thread)
RUN_TEST(test_caps_lock)
RUN_TEST(test_caps_lock_with_shift)
END_TEST_CASE(gfxconsole_keyboard_tests)

}
