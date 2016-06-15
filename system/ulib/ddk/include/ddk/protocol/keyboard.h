// Copyright 2016 The Fuchsia Authors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#include <ddk/protocol/char.h>
#include <runtime/mutex.h>

// Keyboard devices implement the char protocol and send key_event_t's for each keystroke.
// TODO don't use the char protocol when it's possible to wait on non char devices.

// extended keys that aren't pure ascii
enum {
    MX_KEY_RETURN = 0x80,
    MX_KEY_ESC,
    MX_KEY_LSHIFT,
    MX_KEY_RSHIFT,
    MX_KEY_LCTRL,
    MX_KEY_RCTRL,
    MX_KEY_LALT,
    MX_KEY_RALT,
    MX_KEY_CAPSLOCK,
    MX_KEY_LWIN,
    MX_KEY_RWIN,
    MX_KEY_MENU,
    MX_KEY_F1,
    MX_KEY_F2,
    MX_KEY_F3,
    MX_KEY_F4,
    MX_KEY_F5,
    MX_KEY_F6,
    MX_KEY_F7,
    MX_KEY_F8,
    MX_KEY_F9,
    MX_KEY_F10,
    MX_KEY_F11,
    MX_KEY_F12,
    MX_KEY_F13,
    MX_KEY_F14,
    MX_KEY_F15,
    MX_KEY_F16,
    MX_KEY_F17,
    MX_KEY_F18,
    MX_KEY_F19,
    MX_KEY_F20,
    MX_KEY_PRTSCRN,
    MX_KEY_SCRLOCK,
    MX_KEY_PAUSE,
    MX_KEY_TAB,
    MX_KEY_BACKSPACE,
    MX_KEY_INS,
    MX_KEY_DEL,
    MX_KEY_HOME,
    MX_KEY_END,
    MX_KEY_PGUP,
    MX_KEY_PGDN,
    MX_KEY_ARROW_UP,
    MX_KEY_ARROW_DOWN,
    MX_KEY_ARROW_LEFT,
    MX_KEY_ARROW_RIGHT,
    MX_KEY_PAD_NUMLOCK,
    MX_KEY_PAD_DIVIDE,
    MX_KEY_PAD_MULTIPLY,
    MX_KEY_PAD_MINUS,
    MX_KEY_PAD_PLUS,
    MX_KEY_PAD_ENTER,
    MX_KEY_PAD_PERIOD,
    MX_KEY_PAD_0,
    MX_KEY_PAD_1,
    MX_KEY_PAD_2,
    MX_KEY_PAD_3,
    MX_KEY_PAD_4,
    MX_KEY_PAD_5,
    MX_KEY_PAD_6,
    MX_KEY_PAD_7,
    MX_KEY_PAD_8,
    MX_KEY_PAD_9,

    _MX_KEY_LAST,
} extended_keys;


typedef struct mx_key_event {
    uint keycode;
    int pressed;
} mx_key_event_t;

// simple keyboard input queue

#define FIFOSIZE 256
#define FIFOMASK (FIFOSIZE - 1)

typedef struct mx_key_fifo {
    mx_key_event_t events[FIFOSIZE];
    uint32_t head;
    uint32_t tail;
    mxr_mutex_t lock;
} mx_key_fifo_t;

mx_status_t mx_key_fifo_peek(mx_key_fifo_t* fifo, mx_key_event_t** out);
mx_status_t mx_key_fifo_read(mx_key_fifo_t* fifo, mx_key_event_t* out);
mx_status_t mx_key_fifo_write(mx_key_fifo_t* fifo, mx_key_event_t* ev);
