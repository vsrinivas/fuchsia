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

inline vc_char_t vc_char_make(uint8_t ch, uint8_t fg_color, uint8_t bg_color) {
    return static_cast<vc_char_t>(ch | ((fg_color & 0xf) << 8)
                                  | ((bg_color & 0xf) << 12));
}

inline uint8_t vc_char_get_char(vc_char_t ch) {
    return static_cast<uint8_t>(ch & 0xff);
}

inline uint8_t vc_char_get_fg_color(vc_char_t ch) {
    return static_cast<uint8_t>((ch >> 8) & 0xf);
}

inline uint8_t vc_char_get_bg_color(vc_char_t ch) {
    return static_cast<uint8_t>((ch >> 12) & 0xf);
}

typedef enum textcon_param {
    TC_INVALID,
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
    void (*push_scrollback_line)(void* cookie, int y);
    void (*copy_lines)(void* cookie, int y_dest, int y_src, int count);
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

    // Escape sequence parameter parsing
    // Numeric arguments
    int num;  // Argument currently being read
    int argn_count;  // Number of arguments read into argn[]
    int argn[TC_MAX_ARG];
    // String argument (e.g. for console title)
    int argstr_size;  // Number of characters read into argstr[]
    uint8_t argstr[TC_MAX_ARG_LENGTH + 1];
};

void tc_init(textcon_t* tc, int w, int h, vc_char_t* data,
             uint8_t fg, uint8_t bg);

void tc_copy_lines(textcon_t* tc, int y_dest, int y_src, int line_count);

static inline void tc_putc(textcon_t* tc, uint8_t c) {
    tc->putc(tc, c);
}

void tc_seth(textcon_t* tc, int h);
