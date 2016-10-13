// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/device.h>
#include <ddk/common/hid-fifo.h>
#include <gfx/gfx.h>
#include <hid/hid.h>
#include <mxio/vfs.h>
#include <magenta/listnode.h>
#include <stdbool.h>
#include <threads.h>

#include "textcon.h"

typedef uint16_t vc_char_t;
#define CHARVAL(ch, fg, bg) (((ch)&0xff) | (((fg)&0xf) << 8) | (((bg)&0xf) << 12))
#define TOCHAR(ch) ((ch)&0xff)
#define TOFG(ch) (((ch) >> 8) & 0xf)
#define TOBG(ch) (((ch) >> 12) & 0xf)

#define MAX_COLOR 0xf

typedef struct vc_device {
    mx_device_t device;

    mtx_t lock;
    // protect output state of the vc
    // fifo.lock below protects input state

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
    vc_char_t* scrollback_buf;
    // scrollback buffer

    unsigned rows, columns;
    // screen size
    unsigned charw, charh;
    // size of character cell
    unsigned scrollback_rows;
    // number of rows in scrollback

    int invy0, invy1;
    // offscreen invalid lines, tracked during textcon drawing

    unsigned x, y;
    // cursor
    bool hide_cursor;
    // cursor visibility
    int vpy;
    // viewport position, must be <= 0
    unsigned sc_h, sc_t;
    // offsets into the scrollback buffer in rows

    uint32_t palette[16];
    unsigned front_color;
    unsigned back_color;
    // color

    textcon_t textcon;

    mx_hid_fifo_t fifo;
    hid_keys_t key_states[2];
    int key_idx;
    keychar_t* keymap;
    // hid event fifo

    struct list_node node;
    // for virtual console list

    // for char interface
    uint32_t modifiers;
    char chardata[4];
    uint32_t charcount;
} vc_device_t;

#define get_vc_device(dev) containerof(dev, vc_device_t, device)

#define VC_FLAG_HASINPUT    (1 << 0)
#define VC_FLAG_RESETSCROLL (1 << 1)
#define VC_FLAG_FULLSCREEN  (1 << 2)

mx_status_t vc_device_alloc(gfx_surface* hw_gfx, vc_device_t** out_dev);
void vc_device_free(vc_device_t* dev);

mx_status_t vc_set_active_console(unsigned console);
void vc_get_status_line(char* str, int n);

void vc_device_write_status(vc_device_t* dev);
void vc_device_render(vc_device_t* dev);
int vc_device_get_scrollback_lines(vc_device_t* dev);
void vc_device_scroll_viewport(vc_device_t* dev, int dir);
void vc_device_set_fullscreen(vc_device_t* dev, bool fullscreen);

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
void vc_gfx_draw_char(vc_device_t* dev, vc_char_t ch, unsigned x, unsigned y);

static inline uint32_t palette_to_color(vc_device_t* dev, uint8_t color) {
    assert(color <= MAX_COLOR);
    return dev->palette[color];
}

#define MOD_LSHIFT (1 << 0)
#define MOD_RSHIFT (1 << 1)
#define MOD_LALT (1 << 2)
#define MOD_RALT (1 << 3)
#define MOD_LCTRL (1 << 4)
#define MOD_RCTRL (1 << 5)

#define MOD_SHIFT (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_ALT (MOD_LALT | MOD_RALT)
#define MOD_CTRL (MOD_LCTRL | MOD_RCTRL)
