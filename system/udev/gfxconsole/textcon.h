// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <stddef.h>
#include <stdint.h>

#define TC_MAX_ARG 16
#define TC_MAX_ARG_LENGTH 8 // matches vc title length

typedef struct textcon textcon_t;
typedef uint16_t vc_char_t;

typedef enum textcon_param {
    TC_SET_TITLE,
    TC_SHOW_CURSOR,
    TC_HIDE_CURSOR,
} textcon_param_t;

struct textcon {
    void (*putc)(textcon_t* tc, uint8_t c);

    // backing data
    vc_char_t* data;

    // dimensions of display
    int w;
    int h;

    // cursor position
    int x;  // 0 < x <= w; cursor may be one position beyond right edge
    int y;  // 0 < y < h

    // callbacks to update visible display
    void (*invalidate)(void* cookie, int x, int y, int w, int h);
    void (*movecursor)(void* cookie, int x, int y);
    void (*pushline)(void* cookie, int y);
    void (*scroll)(void* cookie, int x, int y0, int y1);
    void (*setparam)(void* cookie, int param, uint8_t* arg, size_t arglen);
    void* cookie;

    // scrolling region
    int scroll_y0;
    int scroll_y1;

    // saved cursor position
    int save_x;
    int save_y;

    uint8_t fg;
    uint8_t bg;

    // escape sequence parameter parsing
    int num;
    int argc;
    int argn[TC_MAX_ARG];
    int argsn;
    uint8_t args[TC_MAX_ARG_LENGTH + 1];
};

void tc_init(textcon_t* tc, int w, int h, void* data, uint8_t fg, uint8_t bg);

static inline void tc_putc(textcon_t* tc, uint8_t c) {
    tc->putc(tc, c);
}

void tc_seth(textcon_t* tc, int h);
