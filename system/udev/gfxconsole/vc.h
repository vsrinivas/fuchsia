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

#include <assert.h>
#include <ddk/device.h>
#include <ddk/protocol/keyboard.h>
#include <gfx/gfx.h>
#include <mxio/vfs.h>
#include <system/listnode.h>
#include <runtime/mutex.h>
#include <stdbool.h>

#include "textcon.h"

typedef uint16_t vc_char_t;
#define CHARVAL(ch, fg, bg) (((ch)&0xff) | (((fg)&0xf) << 8) | (((bg)&0xf) << 12))
#define TOCHAR(ch) ((ch)&0xff)
#define TOFG(ch) (((ch) >> 8) & 0xf)
#define TOBG(ch) (((ch) >> 12) & 0xf)

#define MAX_COLOR 0xf

typedef struct vc_device {
    mx_device_t device;

    mxr_mutex_t lock;
    // protect output state of the vc
    // fifo.lock below protects input state

    char title[8];
    // vc title, shown in status bar
    bool active;
    uint flags;

    // TODO make static
    gfx_surface* gfx;
    // surface to draw on
    gfx_surface* st_gfx;
    // status bar surface
    gfx_surface* hw_gfx;
    // backing store

    vc_char_t* text_buf;
    // text buffer
    vc_char_t* scrollback_buf;
    // scrollback buffer

    uint rows, columns;
    // screen size
    uint scrollback_rows;
    // number of rows in scrollback

    uint x, y;
    // cursor
    bool hide_cursor;
    // cursor visibility
    int vpy;
    // viewport position, must be <= 0
    uint sc_h, sc_t;
    // offsets into the scrollback buffer in rows

    uint32_t palette[16];
    uint front_color;
    uint back_color;
    // color

    textcon_t textcon;

    mx_key_fifo_t fifo;
    // key event fifo

    struct list_node node;
    // for virtual console list

    // for char interface
    uint32_t modifiers;
    char chardata[4];
    uint32_t charcount;
} vc_device_t;

#define get_vc_device(dev) containerof(dev, vc_device_t, device)

#define VC_FLAG_HASINPUT (1 << 0)
#define VC_FLAG_RESETSCROLL (1 << 1)

mx_status_t vc_device_alloc(gfx_surface* hw_gfx, vc_device_t** out_dev);
static inline mx_status_t vc_device_free(vc_device_t* dev) {
    return ERR_NOT_SUPPORTED;
};

mx_status_t vc_set_active_console(uint console);
void vc_get_status_line(char* str, int n);

void vc_device_write_status(vc_device_t* dev);
void vc_device_render(vc_device_t* dev);
int vc_device_get_scrollback_lines(vc_device_t* dev);
void vc_device_scroll_viewport(vc_device_t* dev, int dir);

// drawing:

void vc_gfx_invalidate_all(vc_device_t* dev);
void vc_gfx_invalidate_status(vc_device_t* dev);
void vc_gfx_invalidate(vc_device_t* dev, uint x, uint y, uint w, uint h);
void vc_gfx_draw_char(vc_device_t* dev, vc_char_t ch, uint x, uint y);

static inline uint32_t palette_to_color(vc_device_t* dev, uint8_t color) {
    assert(color <= MAX_COLOR);
    return dev->palette[color];
}

// device protocol:

mx_status_t vc_device_get_protocol(mx_device_t* dev, uint32_t protocol_id, void** protocol);
mx_status_t vc_device_open(mx_device_t* dev, uint32_t flags);
mx_status_t vc_device_close(mx_device_t* dev, void* cookie);
mx_status_t vc_device_release(mx_device_t* dev);

// console protocol:

mx_handle_t vc_console_getsurface(mx_device_t* dev, uint32_t* width, uint32_t* height);
void vc_console_invalidate(mx_device_t* dev, uint32_t x, uint32_t y, uint32_t width, uint32_t height);
void vc_console_movecursor(mx_device_t* dev, uint32_t x, uint32_t y, bool visible);
void vc_console_setpalette(mx_device_t* dev, uint32_t colors[16]);
mx_status_t vc_console_readkey(mx_device_t* dev, uint32_t flags);

// char protocol:

ssize_t vc_char_read(mx_device_t* dev, void* buf, size_t count, size_t off, void* cookie);
ssize_t vc_char_write(mx_device_t* dev, const void* buf, size_t count, size_t off, void* cookie);
ssize_t vc_char_ioctl(mx_device_t* dev, uint32_t op,
                      const void* cmd, size_t cmdlen,
                      void* reply, size_t max, void* cookie);

#define MOD_LSHIFT (1 << 0)
#define MOD_RSHIFT (1 << 1)
#define MOD_LALT (1 << 2)
#define MOD_RALT (1 << 3)
#define MOD_LCTRL (1 << 4)
#define MOD_RCTRL (1 << 5)

#define MOD_SHIFT (MOD_LSHIFT | MOD_RSHIFT)
#define MOD_ALT (MOD_LALT | MOD_RALT)
#define MOD_CTRL (MOD_LCTRL | MOD_RCTRL)
