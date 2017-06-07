// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <threads.h>

#include <magenta/device/input.h>
#include <hid/hid.h>
#include <hid/usages.h>
#include <magenta/syscalls.h>
#include <mxio/io.h>
#include <mxio/watcher.h>

#define VCDEBUG 1

#include "keyboard.h"
#include "vcdebug.h"

#define LOW_REPEAT_KEY_FREQUENCY_MICRO 250000000
#define HIGH_REPEAT_KEY_FREQUENCY_MICRO 50000000

#define DEV_INPUT "/dev/class/input"

static int modifiers_from_keycode(uint8_t keycode) {
    switch (keycode) {
    case HID_USAGE_KEY_LEFT_SHIFT:
        return MOD_LSHIFT;
    case HID_USAGE_KEY_RIGHT_SHIFT:
        return MOD_RSHIFT;
    case HID_USAGE_KEY_LEFT_ALT:
        return MOD_LALT;
    case HID_USAGE_KEY_RIGHT_ALT:
        return MOD_RALT;
    case HID_USAGE_KEY_LEFT_CTRL:
        return MOD_LCTRL;
    case HID_USAGE_KEY_RIGHT_CTRL:
        return MOD_RCTRL;
    }
    return 0;
}

static void vc_process_kb_report(uint8_t* report_buf, hid_keys_t* key_state,
                                 int* cur_idx, int* prev_idx,
                                 hid_keys_t* key_pressed,
                                 hid_keys_t* key_released, int* modifiers,
                                 keypress_handler_t keypress_handler) {
    // process the key
    uint8_t keycode;
    hid_keys_t key_delta;

    hid_kbd_parse_report(report_buf, &key_state[*cur_idx]);
    hid_kbd_pressed_keys(&key_state[*prev_idx], &key_state[*cur_idx], &key_delta);
    if (key_pressed) {
        memcpy(key_pressed, &key_delta, sizeof(key_delta));
    }
    hid_for_every_key(&key_delta, keycode) {
        *modifiers |= modifiers_from_keycode(keycode);
        if (keycode == HID_USAGE_KEY_CAPSLOCK) {
            *modifiers ^= MOD_CAPSLOCK;
        }
        keypress_handler(keycode, *modifiers);
    }

    hid_kbd_released_keys(&key_state[*prev_idx], &key_state[*cur_idx], &key_delta);
    if (key_released) {
        memcpy(key_released, &key_delta, sizeof(key_delta));
    }
    hid_for_every_key(&key_delta, keycode) {
        *modifiers &= ~modifiers_from_keycode(keycode);
    }

    // swap key states
    *cur_idx = 1 - *cur_idx;
    *prev_idx = 1 - *prev_idx;
}

int vc_input_thread(void* arg) {
    auto* args_ptr = reinterpret_cast<vc_input_thread_args*>(arg);
    vc_input_thread_args args = *args_ptr;
    delete args_ptr;

    uint8_t previous_report_buf[8];
    uint8_t report_buf[8];
    hid_keys_t key_state[2];
    memset(&key_state[0], 0, sizeof(hid_keys_t));
    memset(&key_state[1], 0, sizeof(hid_keys_t));
    int cur_idx = 0;
    int prev_idx = 1;
    int modifiers = 0;
    uint64_t repeat_interval = MX_TIME_INFINITE;
    bool repeat_enabled = true;
    char* flag = getenv("gfxconsole.keyrepeat");
    if (flag && (!strcmp(flag, "0") || !strcmp(flag, "false"))) {
        printf("vc: Key repeat disabled\n");
        repeat_enabled = false;
    }

    for (;;) {
        const mx_time_t deadline = (repeat_interval != MX_TIME_INFINITE) ?
                mx_deadline_after(repeat_interval) : MX_TIME_INFINITE;
        mx_status_t rc = mxio_wait_fd(args.fd, MXIO_EVT_READABLE, NULL,
                                      deadline);

        if (rc == ERR_TIMED_OUT) {
            // Times out only when need to repeat.
            vc_process_kb_report(previous_report_buf, key_state,
                                 &cur_idx, &prev_idx, NULL, NULL, &modifiers,
                                 args.keypress_handler);
            vc_process_kb_report(report_buf, key_state, &cur_idx, &prev_idx,
                                 NULL, NULL, &modifiers, args.keypress_handler);
            // Accelerate key repeat until reaching the high frequency
            repeat_interval = repeat_interval * 3 / 4;
            repeat_interval = repeat_interval < HIGH_REPEAT_KEY_FREQUENCY_MICRO ? HIGH_REPEAT_KEY_FREQUENCY_MICRO : repeat_interval;
            continue;
        }

        memcpy(previous_report_buf, report_buf, sizeof(report_buf));
        ssize_t r = read(args.fd, report_buf, sizeof(report_buf));
        if (r <= 0) {
            break; // will be restarted by poll thread if needed
        }
        if ((size_t)(r) != sizeof(report_buf)) {
            repeat_interval = MX_TIME_INFINITE;
            continue;
        }

        hid_keys_t key_pressed, key_released;
        vc_process_kb_report(report_buf, key_state, &cur_idx, &prev_idx,
                             &key_pressed, &key_released, &modifiers,
                             args.keypress_handler);

        if (repeat_enabled) {
            // Check if any non modifiers were pressed
            bool pressed = false, released = false;
            for (int i = 0; i < 7; i++) {
                if (key_pressed.keymask[i]) {
                    pressed = true;
                    break;
                }
            }
            // Check if any key was released
            for (int i = 0; i < 8; i++) {
                if (key_released.keymask[i]) {
                    released = true;
                    break;
                }
            }

            if (released) {
                // Do not repeat released keys, block on next mxio_wait_fd
                repeat_interval = MX_TIME_INFINITE;
            } else if (pressed) {
                // Set timeout on next mxio_wait_fd
                repeat_interval = LOW_REPEAT_KEY_FREQUENCY_MICRO;
            }
        }
    }
    close(args.fd);
    return 0;
}

static mx_status_t vc_input_device_added(int dirfd, int event, const char* fn, void* cookie) {
    if (event != WATCH_EVENT_ADD_FILE) {
        return NO_ERROR;
    }

    auto keypress_handler = reinterpret_cast<keypress_handler_t>(cookie);

    int fd;
    if ((fd = openat(dirfd, fn, O_RDONLY)) < 0) {
        return NO_ERROR;
    }

    printf("vc: new input device %s/%s\n", DEV_INPUT, fn);

    // test to see if this is a device we can read
    int proto = INPUT_PROTO_NONE;
    ssize_t rc = ioctl_input_get_protocol(fd, &proto);
    if (rc > 0 && proto != INPUT_PROTO_KBD) {
        // skip devices that aren't keyboards
        close(fd);
        return NO_ERROR;
    }

    // start a thread to wait on the fd
    char tname[64];
    thrd_t t;
    snprintf(tname, sizeof(tname), "vc-input-%s", fn);
    auto* args = new vc_input_thread_args;
    args->fd = fd;
    args->keypress_handler = keypress_handler;
    int ret = thrd_create_with_name(&t, vc_input_thread, args, tname);
    if (ret != thrd_success) {
        xprintf("vc: input thread %s did not start (return value=%d)\n", tname, ret);
        close(fd);
        delete args;
        // We don't really expect thread creation to fail so it's not clear
        // whether we should return an error here to tell
        // mxio_watch_directory() to stop.
        return NO_ERROR;
    }
    thrd_detach(t);
    return NO_ERROR;
}

void vc_watch_for_keyboard_devices(keypress_handler_t handler) {
    int dirfd;
    if ((dirfd = open(DEV_INPUT, O_DIRECTORY | O_RDONLY)) < 0) {
        return;
    }
    mxio_watch_directory(dirfd, vc_input_device_added,
                         reinterpret_cast<void*>(handler));
    close(dirfd);
}
