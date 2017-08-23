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
#include <port/port.h>
#include <stdbool.h>
#include <threads.h>

#include "textcon.h"

#define MAX_COLOR 0xf

#if BUILD_FOR_TEST
mx_status_t vc_init_gfx(gfx_surface* gfx);
#else
mx_status_t vc_init_gfx(int fd);
void vc_free_gfx();
#endif

typedef void (*keypress_handler_t)(uint8_t keycode, int modifiers);

mx_status_t new_input_device(int fd, keypress_handler_t handler);

// constraints on status bar tabs
#define MIN_TAB_WIDTH 16
#define MAX_TAB_WIDTH 32

#define STATUS_COLOR_BG 0
#define STATUS_COLOR_DEFAULT 7
#define STATUS_COLOR_ACTIVE 11
#define STATUS_COLOR_UPDATED 10

typedef struct vc {
    char title[MAX_TAB_WIDTH];
    // vc title, shown in status bar
    bool active;
    unsigned flags;

    mx_handle_t gfx_vmo;

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

#if !BUILD_FOR_TEST
    port_fd_handler fh;
    mx_handle_t proc;
    bool is_shell;
#endif
} vc_t;

// When VC_FLAG_HASOUTPUT is set, this indicates that there was output to
// the console that hasn't been displayed yet, because this console isn't
// visible.
#define VC_FLAG_HASOUTPUT   (1 << 0)
#define VC_FLAG_FULLSCREEN  (1 << 1)

const gfx_font* vc_get_font();
mx_status_t vc_alloc(vc_t** out, bool special);
void vc_free(vc_t* vc);

// called to re-draw the status bar after
// status-worthy vc or global state has changed
void vc_status_update();

// used by vc_status_invalidate to draw the status bar
void vc_status_clear();
void vc_status_write(int x, unsigned color, const char* text);

void vc_render(vc_t* vc);
void vc_full_repaint(vc_t* vc);
int vc_get_scrollback_lines(vc_t* vc);
vc_char_t* vc_get_scrollback_line_ptr(vc_t* vc, unsigned row);
void vc_scroll_viewport(vc_t* vc, int dir);
void vc_scroll_viewport_top(vc_t* vc);
void vc_scroll_viewport_bottom(vc_t* vc);
void vc_set_fullscreen(vc_t* vc, bool fullscreen);

ssize_t vc_write(vc_t* vc, const void* buf, size_t count,
                        mx_off_t off);

static inline int vc_rows(vc_t* vc) {
    return vc->flags & VC_FLAG_FULLSCREEN ? vc->rows : vc->rows - 1;
}

// drawing:

void vc_gfx_invalidate_all(vc_t* vc);
void vc_gfx_invalidate_status();
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


extern port_t port;
extern bool g_vc_owns_display;
extern vc_t* g_active_vc;
extern int g_status_width;

void handle_key_press(uint8_t keycode, int modifiers);
void vc_toggle_framebuffer();

mx_status_t vc_create(vc_t** out, bool special);
void vc_destroy(vc_t* vc);
ssize_t vc_write(vc_t* vc, const void* buf, size_t count, mx_off_t off);
mx_status_t vc_set_active(int num, vc_t* vc);
