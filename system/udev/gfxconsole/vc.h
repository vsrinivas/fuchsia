// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <assert.h>
#include <ddk/device.h>
#include <ddk/common/hid-fifo.h>
#include <gfx/gfx.h>
#include <hid/hid.h>
#include <mxio/vfs.h>
#include <magenta/listnode.h>
#include <magenta/thread_annotations.h>
#include <stdbool.h>
#include <threads.h>

#include "textcon.h"

#define MAX_COLOR 0xf

typedef struct vc_device {
    mx_device_t* mxdev;

    char title[8];
    // vc title, shown in status bar
    bool active;
    unsigned flags;

    mx_handle_t gfx_vmo;

    // TODO make static
    gfx_surface* gfx;
    // surface to draw on
    gfx_surface* st_gfx;
    // status bar surface
    gfx_surface* hw_gfx;
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

    mx_hid_fifo_t fifo;
    // FIFO for storing keyboard input.  Note that this stores characters,
    // not HID events.
    keychar_t* keymap;

    struct list_node node;
    // for virtual console list
} vc_device_t;

// When VC_FLAG_HASOUTPUT is set, this indicates that there was output to
// the console that hasn't been displayed yet, because this console isn't
// visible.
#define VC_FLAG_HASOUTPUT   (1 << 0)
#define VC_FLAG_FULLSCREEN  (1 << 1)

extern mtx_t g_vc_lock;

const gfx_font* vc_get_font();
mx_status_t vc_device_alloc(gfx_surface* hw_gfx, vc_device_t** out_dev);
void vc_device_free(vc_device_t* dev);

void vc_get_status_line(char* str, int n) TA_REQ(g_vc_lock);

enum vc_battery_state {
    UNAVAILABLE = 0, NOT_CHARGING, CHARGING, ERROR
};

typedef struct vc_battery_info {
    enum vc_battery_state state;
    int pct;
} vc_battery_info_t;
void vc_get_battery_info(vc_battery_info_t* info) TA_REQ(g_vc_lock);

void vc_device_write_status(vc_device_t* dev) TA_REQ(g_vc_lock);
void vc_device_render(vc_device_t* dev) TA_REQ(g_vc_lock);
void vc_device_invalidate_all_for_testing(vc_device_t* dev);
int vc_device_get_scrollback_lines(vc_device_t* dev);
vc_char_t* vc_device_get_scrollback_line_ptr(vc_device_t* dev, unsigned row);
void vc_device_scroll_viewport(vc_device_t* dev, int dir) TA_REQ(g_vc_lock);
void vc_device_scroll_viewport_top(vc_device_t* dev) TA_REQ(g_vc_lock);
void vc_device_scroll_viewport_bottom(vc_device_t* dev) TA_REQ(g_vc_lock);
void vc_device_set_fullscreen(vc_device_t* dev, bool fullscreen)
    TA_REQ(g_vc_lock);

ssize_t vc_device_write(vc_device_t* dev, const void* buf, size_t count,
                        mx_off_t off);

static inline int vc_device_rows(vc_device_t* dev) {
    return dev->flags & VC_FLAG_FULLSCREEN ? dev->rows : dev->rows - 1;
}

// drawing:

void vc_gfx_invalidate_all(vc_device_t* dev);
void vc_gfx_invalidate_status(vc_device_t* dev);
// invalidates a region in characters
void vc_gfx_invalidate(vc_device_t* dev, unsigned x, unsigned y, unsigned w, unsigned h);
// invalidates a region in pixels
void vc_gfx_invalidate_region(vc_device_t* dev, unsigned x, unsigned y, unsigned w, unsigned h);
void vc_gfx_draw_char(vc_device_t* dev, vc_char_t ch, unsigned x, unsigned y,
                      bool invert);

static inline uint32_t palette_to_color(vc_device_t* dev, uint8_t color) {
    assert(color <= MAX_COLOR);
    return dev->palette[color];
}
