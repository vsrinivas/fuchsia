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

bool test_keyboard_input_thread() {
    BEGIN_TEST;

    int pipe_fds[2];
    int rc = pipe(pipe_fds);
    EXPECT_EQ(rc, 0, "");

    auto* args = new vc_input_thread_args;
    args->fd = pipe_fds[1];
    args->keypress_handler = keypress_handler;
    thrd_t thread;
    int ret = thrd_create_with_name(&thread, vc_input_thread, args, "input");
    EXPECT_EQ(ret, thrd_success, "");

    // USB HID key state buffer.
    uint8_t report_buf[8] = {};
    // Byte 0 contains one bit per modifier key.
    uint8_t* modifiers_byte = &report_buf[0];
    // Bytes 2+ contain USB HID key codes.
    uint8_t* first_keycode = &report_buf[2];

    auto write_report_buf = [&]() {
        EXPECT_EQ(write(pipe_fds[0], report_buf, sizeof(report_buf)),
                  sizeof(report_buf), "");
    };

    // Test pressing keys without any modifiers.
    *first_keycode = HID_USAGE_KEY_M;
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_M, 0, 'm');

    // Test autorepeat: After some delay, the same key should be reported
    // again.
    expect_keypress(HID_USAGE_KEY_M, 0, 'm');

    *first_keycode = HID_USAGE_KEY_6;
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_6, 0, '6');

    // Press a modifier (but no other keys).
    *first_keycode = 0; // Unset the earlier key
    *modifiers_byte = 2; // Left Shift key
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_LEFT_SHIFT, MOD_LSHIFT, '\0');

    // Test keys with modifiers pressed.
    // Test Shift-N.
    *first_keycode = HID_USAGE_KEY_N;
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_N, MOD_LSHIFT, 'N');

    // Test Shift-8.
    *first_keycode = HID_USAGE_KEY_8;
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_8, MOD_LSHIFT, '*');

    // Test Ctrl modifier.  First send a separate report_buf event to
    // report unsetting the Shift key state, to account for a quirk of the
    // current implementation.
    *modifiers_byte = 0;
    write_report_buf();
    *modifiers_byte = 1; // Left Ctrl key
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_LEFT_CTRL, MOD_LCTRL, '\0');

    // Test Ctrl-J.
    *first_keycode = HID_USAGE_KEY_J;
    write_report_buf();
    expect_keypress(HID_USAGE_KEY_J, MOD_LCTRL, 10);

    ret = close(pipe_fds[0]);
    EXPECT_EQ(ret, 0, "");

    // Test that the keyboard input thread exits properly after it reads EOF.
    EXPECT_EQ(thrd_join(thread, &ret), thrd_success, "");

    END_TEST;
}

BEGIN_TEST_CASE(gfxconsole_keyboard_tests)
RUN_TEST(test_keyboard_input_thread)
END_TEST_CASE(gfxconsole_keyboard_tests)

}
