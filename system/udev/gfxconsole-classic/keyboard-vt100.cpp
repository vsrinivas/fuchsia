// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <hid/usages.h>

#include "keyboard-vt100.h"
#include "keyboard.h"

uint32_t hid_key_to_vt100_code(uint8_t keycode, int modifiers,
                               keychar_t* keymap, char* buf, size_t buf_size) {
    // Consistency check: Max size of byte sequences we produce below.
    if (buf_size != 4)
        return 0;

    uint8_t ch = hid_map_key(keycode, modifiers & MOD_SHIFT, keymap);
    if (ch) {
        if (modifiers & MOD_CTRL) {
            // Handle Ctrl-A to Ctrl-Z.  Ignore the Ctrl modifier on any
            // other keys.
            uint8_t range_start = modifiers & MOD_SHIFT ? 'A' : 'a';
            uint8_t range_end = static_cast<uint8_t>(range_start + 26);
            if (range_start <= ch && ch < range_end) {
                buf[0] = static_cast<char>(ch - range_start + 1);
                return 1;
            }
        }
        if (modifiers & MOD_CAPSLOCK) {
            if ('a' <= ch && ch <= 'z') {
                ch = static_cast<char>(ch  - 'a' + 'A');
            } else if ('A' <= ch && ch <= 'Z') {
                ch = static_cast<char>(ch  - 'A' + 'a');
            }
        }
        buf[0] = ch;
        return 1;
    }

    switch (keycode) {
    // generate special stuff for a few different keys
    case HID_USAGE_KEY_ENTER:
    case HID_USAGE_KEY_KP_ENTER:
        buf[0] = '\n';
        return 1;
    case HID_USAGE_KEY_BACKSPACE:
        buf[0] = '\b';
        return 1;
    case HID_USAGE_KEY_TAB:
        buf[0] = '\t';
        return 1;
    case HID_USAGE_KEY_ESC:
        buf[0] = 0x1b;
        return 1;

    // generate vt100 key codes for arrows
    case HID_USAGE_KEY_UP:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 65;
        return 3;
    case HID_USAGE_KEY_DOWN:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 66;
        return 3;
    case HID_USAGE_KEY_RIGHT:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 67;
        return 3;
    case HID_USAGE_KEY_LEFT:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 68;
        return 3;
    case HID_USAGE_KEY_HOME:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 'H';
        return 3;
    case HID_USAGE_KEY_END:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = 'F';
        return 3;
    case HID_USAGE_KEY_DELETE:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = '3';
        buf[3] = '~';
        return 4;
    case HID_USAGE_KEY_PAGEUP:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = '5';
        buf[3] = '~';
        return 4;
    case HID_USAGE_KEY_PAGEDOWN:
        buf[0] = 0x1b;
        buf[1] = '[';
        buf[2] = '6';
        buf[3] = '~';
        return 4;
    }
    // ignore unknown keys; character keys were handled above
    return 0;
}
