// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>

#include <gfx/gfx.h>
#include <hid/hid.h>
#include <mxio/vfs.h>
#include <magenta/listnode.h>
#include <magenta/thread_annotations.h>
#include <stdbool.h>
#include <threads.h>

#include "textcon.h"

#define MAX_COLOR 0xf

typedef struct vc {
    char title[8];
    // vc title, shown in status bar
    bool active;
    unsigned flags;

    mx_handle_t gfx_vmo;

    // TODO make static
    gfx_surface* gfx;
    // surface to draw on
    gfx_surface* st_gfx;

#if BUILD_FOR_TEST
    gfx_surface* test_gfx;
#endif

    int fd;

    // backing store
    const gfx_font* font;

    vc_char_t* text_buf;
    // text buffer

    // Buffer containing scrollback lines.  This is a circular buffer.
    vc_char_t* scrollback_buf;
    // Maximum number of rows that may be stored in the scrollback buffer.
    unsigned scrollback_rows_max;
    // Number of rows currently stored in the scrollback buffer.
    unsigned scrollback_rows_count;
    // Offset, in rows, of the oldest row in the scrollback buffer.
    unsigned scrollback_offset;

    unsigned rows, columns;
    // screen size
    unsigned charw, charh;
    // size of character cell

    int invy0, invy1;
    // offscreen invalid lines, tracked during textcon drawing

    unsigned cursor_x, cursor_y;
    // cursor
    bool hide_cursor;
    // cursor visibility
    int viewport_y;
    // viewport position, must be <= 0

    uint32_t palette[16];
    uint8_t front_color;
    uint8_t back_color;
    // color

    textcon_t textcon;

    keychar_t* keymap;

    struct list_node node;
    // for virtual console list
} vc_t;

// When VC_FLAG_HASOUTPUT is set, this indicates that there was output to
// the console that hasn't been displayed yet, because this console isn't
// visible.
#define VC_FLAG_HASOUTPUT   (1 << 0)
#define VC_FLAG_FULLSCREEN  (1 << 1)

extern mtx_t g_vc_lock;

const gfx_font* vc_get_font();
mx_status_t vc_alloc(gfx_surface* test, int fd, vc_t** out_dev);
void vc_free(vc_t* vc);

void vc_get_status_line(char* str, int n) TA_REQ(g_vc_lock);

enum vc_battery_state {
    UNAVAILABLE = 0, NOT_CHARGING, CHARGING, ERROR
};

typedef struct vc_battery_info {
    enum vc_battery_state state;
    int pct;
} vc_battery_info_t;
void vc_get_battery_info(vc_battery_info_t* info) TA_REQ(g_vc_lock);

void vc_write_status(vc_t* vc) TA_REQ(g_vc_lock);
void vc_render(vc_t* vc) TA_REQ(g_vc_lock);
void vc_invalidate_all_for_testing(vc_t* vc);
int vc_get_scrollback_lines(vc_t* vc);
vc_char_t* vc_get_scrollback_line_ptr(vc_t* vc, unsigned row);
void vc_scroll_viewport(vc_t* vc, int dir) TA_REQ(g_vc_lock);
void vc_scroll_viewport_top(vc_t* vc) TA_REQ(g_vc_lock);
void vc_scroll_viewport_bottom(vc_t* vc) TA_REQ(g_vc_lock);
void vc_set_fullscreen(vc_t* vc, bool fullscreen)
    TA_REQ(g_vc_lock);

ssize_t vc_write(vc_t* vc, const void* buf, size_t count,
                        mx_off_t off);

static inline int vc_rows(vc_t* vc) {
    return vc->flags & VC_FLAG_FULLSCREEN ? vc->rows : vc->rows - 1;
}

// drawing:

void vc_gfx_invalidate_all(vc_t* vc);
void vc_gfx_invalidate_status(vc_t* vc);
// invalidates a region in characters
void vc_gfx_invalidate(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h);
// invalidates a region in pixels
void vc_gfx_invalidate_region(vc_t* vc, unsigned x, unsigned y, unsigned w, unsigned h);
void vc_gfx_draw_char(vc_t* vc, vc_char_t ch, unsigned x, unsigned y,
                      bool invert);

static inline uint32_t palette_to_color(vc_t* vc, uint8_t color) {
    assert(color <= MAX_COLOR);
    return vc->palette[color];
}
