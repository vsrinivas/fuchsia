// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <hid/hid.h>
#include <hid/usages.h>

#include <magenta/device/input.h>
#include <magenta/syscalls.h>

#include <port/port.h>

#include "keyboard.h"

#define LOW_REPEAT_KEY_FREQ 250000000
#define HIGH_REPEAT_KEY_FREQ 50000000

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

static void set_caps_lock_led(int keyboard_fd, bool caps_lock) {
    // The following bit to set is specified in "Device Class Definition
    // for Human Interface Devices (HID)", Version 1.11,
    // http://www.usb.org/developers/hidpage/HID1_11.pdf.  Magenta leaves
    // USB keyboards in boot mode, so the relevant section is Appendix B,
    // "Boot Interface Descriptors", "B.1 Protocol 1 (Keyboard)".
    const int kUsbCapsLockBit = 1 << 1;

    const int kNumBytes = 1;
    uint8_t msg_buf[sizeof(input_set_report_t) + kNumBytes];
    auto* msg = reinterpret_cast<input_set_report_t*>(msg_buf);
    msg->id = 0;
    msg->type = INPUT_REPORT_OUTPUT;
    msg->data[0] = caps_lock ? kUsbCapsLockBit : 0;
    ssize_t result = ioctl_input_set_report(keyboard_fd, msg, sizeof(msg_buf));
    if (result != kNumBytes) {
#if !BUILD_FOR_TEST
        printf("ioctl_input_set_report() failed (returned %zd)\n", result);
#endif
    }
}

struct vc_input {
    port_fd_handler_t fh;
    port_handler_t th;
    mx_handle_t timer;

    keypress_handler_t handler;
    int fd;

    uint8_t previous_report_buf[8];
    uint8_t report_buf[8];
    hid_keys_t state[2];
    int cur_idx;
    int prev_idx;
    int modifiers;
    uint64_t repeat_interval;
    bool repeat_enabled;
};

// returns true if key was pressed and none were released
bool vc_input_process(vc_input_t* vi, uint8_t report[8]) {
    bool do_repeat = false;

    // process the key
    uint8_t keycode;
    hid_keys_t keys;

    hid_kbd_parse_report(report, &vi->state[vi->cur_idx]);

    hid_kbd_pressed_keys(&vi->state[vi->prev_idx], &vi->state[vi->cur_idx], &keys);
    hid_for_every_key(&keys, keycode) {
        vi->modifiers |= modifiers_from_keycode(keycode);
        if (keycode == HID_USAGE_KEY_CAPSLOCK) {
            vi->modifiers ^= MOD_CAPSLOCK;
            set_caps_lock_led(vi->fd, vi->modifiers & MOD_CAPSLOCK);
        }
        vi->handler(keycode, vi->modifiers);
        do_repeat = true;
    }

    hid_kbd_released_keys(&vi->state[vi->prev_idx], &vi->state[vi->cur_idx], &keys);
    hid_for_every_key(&keys, keycode) {
        vi->modifiers &= ~modifiers_from_keycode(keycode);
        do_repeat = false;
    }

    // swap key states
    vi->cur_idx = 1 - vi->cur_idx;
    vi->prev_idx = 1 - vi->prev_idx;

    return do_repeat;
}

#if !BUILD_FOR_TEST
static void vc_input_destroy(vc_input_t* vi) {
    port_cancel(&port, &vi->th);
    if (vi->fd >= 0) {
        port_fd_handler_done(&vi->fh);
        close(vi->fd);
    }
    mx_handle_close(vi->timer);
    free(vi);
}

static mx_status_t vc_timer_cb(port_handler_t* ph, mx_signals_t signals, uint32_t evt) {
    vc_input_t* vi = containerof(ph, vc_input_t, th);

    // if interval is infinite, repeat was canceled
    if (vi->repeat_interval != MX_TIME_INFINITE) {
        vc_input_process(vi, vi->previous_report_buf);
        vc_input_process(vi, vi->report_buf);

        // increase repeat rate if we're not yet at the fastest rate
        if ((vi->repeat_interval = vi->repeat_interval * 3 / 4) < HIGH_REPEAT_KEY_FREQ) {
            vi->repeat_interval = HIGH_REPEAT_KEY_FREQ;
        }

        mx_timer_set(vi->timer, mx_deadline_after(vi->repeat_interval), 0);
    }

    // We've set this up as repeating so we always
    // return an error to avoid the auto-re-arm behaviour
    // of the port library
    return MX_ERR_STOP;
}

static mx_status_t vc_input_cb(port_fd_handler_t* fh, unsigned pollevt, uint32_t evt) {
    vc_input_t* vi = containerof(fh, vc_input_t, fh);
    ssize_t r;

    if (!(pollevt & POLLIN)) {
        r = MX_ERR_PEER_CLOSED;
    } else {
        memcpy(vi->previous_report_buf, vi->report_buf, sizeof(vi->report_buf));
        r = read(vi->fd, vi->report_buf, sizeof(vi->report_buf));
    }
    if (r <= 0) {
        vc_input_destroy(vi);
        return MX_ERR_STOP;
    }
    if ((size_t)(r) != sizeof(vi->report_buf)) {
        vi->repeat_interval = MX_TIME_INFINITE;
        return MX_OK;
    }

    if (vc_input_process(vi, vi->report_buf) && vi->repeat_enabled) {
        vi->repeat_interval = LOW_REPEAT_KEY_FREQ;
        mx_timer_set(vi->timer, mx_deadline_after(vi->repeat_interval), 0);
    } else {
        vi->repeat_interval = MX_TIME_INFINITE;
    }
    return MX_OK;
}
#endif

mx_status_t vc_input_create(vc_input_t** out, keypress_handler_t handler, int fd) {
    vc_input_t* vi = reinterpret_cast<vc_input_t*>(calloc(1, sizeof(vc_input_t)));
    if (vi == NULL) {
        return MX_ERR_NO_MEMORY;
    }

    vi->fd = fd;
    vi->handler = handler;

    vi->cur_idx = 0;
    vi->prev_idx = 1;
    vi->modifiers = 0;
    vi->repeat_interval = MX_TIME_INFINITE;
    vi->repeat_enabled = true;

    char* flag = getenv("virtcon.keyrepeat");
    if (flag && (!strcmp(flag, "0") || !strcmp(flag, "false"))) {
        printf("vc: Key repeat disabled\n");
        vi->repeat_enabled = false;
    }

#if !BUILD_FOR_TEST
    mx_status_t r;
    if ((r = mx_timer_create(0, MX_CLOCK_MONOTONIC, &vi->timer)) < 0) {
        free(vi);
        return r;
    }

    vi->fh.func = vc_input_cb;
    if ((r = port_fd_handler_init(&vi->fh, fd, POLLIN | POLLHUP | POLLRDHUP)) < 0) {
        mx_handle_close(vi->timer);
        free(vi);
        return r;
    }

    if ((r = port_wait(&port, &vi->fh.ph)) < 0) {
        port_fd_handler_done(&vi->fh);
        mx_handle_close(vi->timer);
        free(vi);
        return r;
    }

    vi->th.handle = vi->timer;
    vi->th.waitfor = MX_TIMER_SIGNALED;
    vi->th.func = vc_timer_cb;
    port_wait_repeating(&port, &vi->th);
#endif

    *out = vi;
    return MX_OK;
}

#if !BUILD_FOR_TEST
mx_status_t new_input_device(int fd, keypress_handler_t handler) {
    // test to see if this is a device we can read
    int proto = INPUT_PROTO_NONE;
    ssize_t rc = ioctl_input_get_protocol(fd, &proto);
    if ((rc < 0) || (proto != INPUT_PROTO_KBD)) {
        // skip devices that aren't keyboards
        close(fd);
        return MX_ERR_NOT_SUPPORTED;
    }

    mx_status_t r;
    vc_input_t* vi;
    if ((r = vc_input_create(&vi, handler, fd)) < 0) {
        close(fd);
    }
    return r;
}
#endif
