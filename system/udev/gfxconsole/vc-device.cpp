// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <magenta/process.h>

#define VCDEBUG 0

#include "vc.h"
#include "vcdebug.h"

static uint32_t default_palette[] = {
    // 0-7 Normal/dark versions of colors
    0xff000000, // black
    0xffaa0000, // red
    0xff00aa00, // green
    0xffaa5500, // brown
    0xff0000aa, // blue
    0xffaa00aa, // magenta
    0xff00aaaa, // cyan
    0xffaaaaaa, // grey
    // 8-15 Bright/light versions of colors
    0xff555555, // dark grey
    0xffff5555, // bright red
    0xff55ff55, // bright green
    0xffffff55, // yellow
    0xff5555ff, // bright blue
    0xffff55ff, // bright magenta
    0xff55ffff, // bright cyan
    0xffffffff, // white
};

#define DEFAULT_FRONT_COLOR 0x0 // black
#define DEFAULT_BACK_COLOR 0xf  // white

#define SCROLLBACK_ROWS 1024 // TODO make configurable

#define ABS(val) (((val) >= 0) ? (val) : -(val))

static mx_status_t vc_device_setup(vc_device_t* dev) {
    assert(dev->gfx);
    assert(dev->hw_gfx);

    mtx_init(&dev->lock, mtx_plain);

    // calculate how many rows/columns we have
    dev->rows = dev->gfx->height / dev->charh;
    dev->columns = dev->gfx->width / dev->charw;
    dev->scrollback_rows = SCROLLBACK_ROWS;

    // allocate the text buffer
    dev->text_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, dev->rows * dev->columns * sizeof(vc_char_t)));
    if (!dev->text_buf)
        return ERR_NO_MEMORY;

    // allocate the scrollback buffer
    dev->scrollback_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, dev->scrollback_rows * dev->columns * sizeof(vc_char_t)));
    if (!dev->scrollback_buf) {
        free(dev->text_buf);
        return ERR_NO_MEMORY;
    }

    // set up the default palette
    memcpy(&dev->palette, default_palette, sizeof(default_palette));
    dev->front_color = DEFAULT_FRONT_COLOR;
    dev->back_color = DEFAULT_BACK_COLOR;

    return NO_ERROR;
}

static void vc_device_invalidate(void* cookie, int x0, int y0, int w, int h) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);

    assert(y0 <= static_cast<int>(dev->rows));
    assert(h >= 0);
    assert(y0 + h <= static_cast<int>(dev->rows));

    for (int y = y0; y < y0 + h; y++) {
        int sc = 0;
        if (y < 0) {
            sc = dev->scrollback_tail + y;
            if (sc < 0)
                sc += dev->scrollback_rows;
        }
        for (int x = x0; x < x0 + w; x++) {
            if (y < 0) {
                vc_gfx_draw_char(dev, dev->scrollback_buf[x + sc * dev->columns],
                                 x, y - dev->viewport_y, /* invert= */ false);
            } else {
                // Check whether we should display the cursor at this
                // position.  Note that it's possible that the cursor is
                // outside the display area (dev->cursor_x ==
                // dev->columns).  In that case, we won't display the
                // cursor, even if there's a margin.  This matches
                // gnome-terminal.
                bool invert = (!dev->hide_cursor &&
                               static_cast<unsigned>(x) == dev->cursor_x &&
                               static_cast<unsigned>(y) == dev->cursor_y);
                vc_gfx_draw_char(dev, dev->text_buf[x + y * dev->columns],
                                 x, y - dev->viewport_y, invert);
            }
        }
    }
}

// implement tc callbacks:

static inline void vc_invalidate_lines(vc_device_t* dev, int y, int h) {
    if (y < dev->invy0) {
        dev->invy0 = y;
    }
    y += h;
    if (y > dev->invy1) {
        dev->invy1 = y;
    }
}

static void vc_tc_invalidate(void* cookie, int x0, int y0, int w, int h) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    if (dev->flags & VC_FLAG_RESETSCROLL) {
        dev->flags &= ~VC_FLAG_RESETSCROLL;
        vc_device_scroll_viewport(dev, -dev->viewport_y);
    }
    if (dev->viewport_y < 0)
        return;
    vc_device_invalidate(cookie, x0, y0, w, h);
    vc_invalidate_lines(dev, y0, h);
}

static void vc_tc_movecursor(void* cookie, int x, int y) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    unsigned old_x = dev->cursor_x;
    unsigned old_y = dev->cursor_y;
    dev->cursor_x = x;
    dev->cursor_y = y;
    if (!dev->hide_cursor) {
        // Clear the cursor from its old position.
        vc_device_invalidate(cookie, old_x, old_y, 1, 1);
        vc_invalidate_lines(dev, old_y, 1);

        // Display the cursor in its new position.
        vc_device_invalidate(cookie, dev->cursor_x, dev->cursor_y, 1, 1);
        vc_invalidate_lines(dev, dev->cursor_y, 1);
    }
}

static void vc_tc_pushline(void* cookie, int y) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    vc_char_t* dst = &dev->scrollback_buf[dev->scrollback_tail * dev->columns];
    vc_char_t* src = &dev->text_buf[y * dev->columns];
    memcpy(dst, src, dev->columns * sizeof(vc_char_t));
    dev->scrollback_tail += 1;
    if (dev->viewport_y < 0)
        dev->viewport_y -= 1;
    if (dev->scrollback_tail >= dev->scrollback_rows) {
        dev->scrollback_tail -= dev->scrollback_rows;
        if (dev->scrollback_tail >= dev->scrollback_head)
            dev->scrollback_head = dev->scrollback_tail + 1;
    }
}

static void vc_set_cursor_hidden(vc_device_t* dev, bool hide) {
    if (dev->hide_cursor == hide)
        return;
    dev->hide_cursor = hide;
    vc_device_invalidate(dev, dev->cursor_x, dev->cursor_y, 1, 1);
    vc_invalidate_lines(dev, dev->cursor_y, 1);
}

static void vc_tc_copy_lines(void* cookie, int y_dest, int y_src,
                             int line_count) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    if (dev->viewport_y < 0)
        return;

    // Remove the cursor from the display before copying the lines on
    // screen, otherwise we might be copying a rendering of the cursor to a
    // position where the cursor isn't.  This must be done before the
    // tc_copy_lines() call, otherwise we might render the wrong character.
    bool old_hide_cursor = dev->hide_cursor;
    vc_set_cursor_hidden(dev, true);

    // The next two calls can be done in any order.
    tc_copy_lines(&dev->textcon, y_dest, y_src, line_count);
    gfx_copyrect(dev->gfx, 0, y_src * dev->charh,
                 dev->gfx->width, line_count * dev->charh,
                 0, y_dest * dev->charh);

    // Restore the cursor.
    vc_set_cursor_hidden(dev, old_hide_cursor);

    vc_device_write_status(dev);
    vc_gfx_invalidate_status(dev);
    vc_invalidate_lines(dev, 0, vc_device_rows(dev));
}

static void vc_tc_setparam(void* cookie, int param, uint8_t* arg, size_t arglen) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    switch (param) {
    case TC_SET_TITLE:
        strncpy(dev->title, (char*)arg, sizeof(dev->title));
        dev->title[sizeof(dev->title) - 1] = '\0';
        vc_device_write_status(dev);
        vc_gfx_invalidate_status(dev);
        break;
    case TC_SHOW_CURSOR:
        vc_set_cursor_hidden(dev, false);
        break;
    case TC_HIDE_CURSOR:
        vc_set_cursor_hidden(dev, true);
        break;
    default:; // nothing
    }
}

static void vc_device_clear_gfx(vc_device_t* dev) {
    // Fill display with background color
    gfx_fillrect(dev->gfx, 0, 0, dev->gfx->width, dev->gfx->height,
                 palette_to_color(dev, dev->back_color));
}

static void vc_device_reset(vc_device_t* dev) {
    // reset the cursor
    dev->cursor_x = 0;
    dev->cursor_y = 0;
    // reset the viewport position
    dev->viewport_y = 0;

    tc_init(&dev->textcon, dev->columns, vc_device_rows(dev), dev->text_buf, dev->front_color, dev->back_color);
    dev->textcon.cookie = dev;
    dev->textcon.invalidate = vc_tc_invalidate;
    dev->textcon.movecursor = vc_tc_movecursor;
    dev->textcon.pushline = vc_tc_pushline;
    dev->textcon.copy_lines = vc_tc_copy_lines;
    dev->textcon.setparam = vc_tc_setparam;

    // fill textbuffer with blank characters
    size_t count = dev->rows * dev->columns;
    vc_char_t* ptr = dev->text_buf;
    while (count--) {
        *ptr++ = CHARVAL(' ', dev->front_color, dev->back_color);
    }

    vc_device_clear_gfx(dev);
    gfx_flush(dev->gfx);

    vc_gfx_invalidate_all(dev);
}

#define STATUS_FG 7
#define STATUS_BG 0

static void write_status_at(vc_device_t* dev, const char* str, unsigned offset) {
    static enum { NORMAL,
                  ESCAPE } state = NORMAL;
    uint8_t fg = STATUS_FG;
    uint8_t bg = STATUS_BG;
    char c;
    int idx = offset;
    int p_num = 0;
    for (unsigned i = 0; i < strlen(str); i++) {
        c = str[i];
        if (state == NORMAL) {
            if (c == 0x1b) {
                state = ESCAPE;
                p_num = 0;
            } else {
                gfx_putchar(dev->st_gfx, dev->font, c, idx++ * dev->charw, 0,
                            palette_to_color(dev, fg), palette_to_color(dev, bg));
            }
        } else if (state == ESCAPE) {
            if (c >= '0' && c <= '9') {
                p_num = (p_num * 10) + (c - '0');
            } else if (c == 'm') {
                if (p_num >= 30 && p_num <= 37) {
                    fg = (uint8_t)(p_num - 30);
                } else if (p_num >= 40 && p_num <= 47) {
                    bg = (uint8_t)(p_num - 40);
                } else if (p_num == 1 && fg <= 0x7) {
                    fg = (uint8_t)(fg + 8);
                } else if (p_num == 0) {
                    fg = STATUS_FG;
                    bg = STATUS_BG;
                }
                state = NORMAL;
            } else {
                // eat unrecognized escape sequences in status
            }
        }
    }
}

void vc_device_write_status(vc_device_t* dev) {
    char str[512];

    // draw the tabs
    vc_get_status_line(str, sizeof(str));
    // TODO clean this up with textcon stuff
    gfx_fillrect(dev->st_gfx, 0, 0, dev->st_gfx->width, dev->st_gfx->height, palette_to_color(dev, STATUS_BG));
    write_status_at(dev, str, 0);

    // draw the battery status
    vc_battery_info_t info;
    vc_get_battery_info(&info);
    switch (info.state) {
        case ERROR:
            snprintf(str, sizeof(str), "err");
            break;
        case CHARGING:
            snprintf(str, sizeof(str), "\033[33m\033[1mc %d%%", info.pct);
            break;
        case NOT_CHARGING:
            if (info.pct <= 20) {
                snprintf(str, sizeof(str), "\033[31m\033[1m%d%%", info.pct);
            } else {
                snprintf(str, sizeof(str), "%d%%", info.pct);
            }
            break;
        default:
            str[0] = '\0';
            break;
    }
    write_status_at(dev, str, dev->columns - 8);

    gfx_flush(dev->st_gfx);
}

void vc_device_render(vc_device_t* dev) {
    vc_device_write_status(dev);
    vc_gfx_invalidate_all(dev);
}

void vc_device_invalidate_all_for_testing(vc_device_t* dev) {
    vc_device_clear_gfx(dev);
    vc_device_invalidate(dev, 0, 0, dev->columns, dev->rows);
    // Restore the cursor.
    vc_tc_movecursor(dev, dev->cursor_x, dev->cursor_y);
}

int vc_device_get_scrollback_lines(vc_device_t* dev) {
    if (dev->scrollback_tail >= dev->scrollback_head)
        return dev->scrollback_tail - dev->scrollback_head;
    return dev->scrollback_rows - 1;
}

void vc_device_scroll_viewport(vc_device_t* dev, int dir) {
    int vpy = MAX(MIN(dev->viewport_y + dir, 0),
                  -vc_device_get_scrollback_lines(dev));
    int delta = ABS(dev->viewport_y - vpy);
    if (delta == 0)
        return;
    dev->viewport_y = vpy;
    unsigned rows = vc_device_rows(dev);
    if (dir > 0) {
        gfx_copyrect(dev->gfx, 0, delta * dev->charh,
                     dev->gfx->width, (rows - delta) * dev->charh, 0, 0);
        vc_device_invalidate(dev, 0, vpy + rows - delta, dev->columns, delta);
    } else {
        gfx_copyrect(dev->gfx, 0, 0, dev->gfx->width,
                     (rows - delta) * dev->charh, 0, delta * dev->charh);
        vc_device_invalidate(dev, 0, vpy, dev->columns, delta);
    }
    gfx_flush(dev->gfx);
    vc_device_render(dev);
}

void vc_device_set_fullscreen(vc_device_t* dev, bool fullscreen) {
    mtx_lock(&dev->lock);
    unsigned flags;
    if (fullscreen) {
        flags = dev->flags | VC_FLAG_FULLSCREEN;
    } else {
        flags = dev->flags & ~VC_FLAG_FULLSCREEN;
    }
    if (flags != dev->flags) {
        dev->flags = flags;
        tc_seth(&dev->textcon, vc_device_rows(dev));
    }
    mtx_unlock(&dev->lock);
    vc_device_render(dev);
}

const gfx_font* vc_get_font() {
    char* fname = getenv("gfxconsole.font");
    if (fname) {
        if (!strcmp(fname, "9x16")) {
            return &font9x16;
        } else if (!strcmp(fname, "18x32")) {
            return &font18x32;
        } else {
            printf("gfxconsole: no such font '%s'\n", fname);
        }
    }
    return &font9x16;
}

mx_status_t vc_device_alloc(gfx_surface* hw_gfx, vc_device_t** out_dev) {
    vc_device_t* device =
        reinterpret_cast<vc_device_t*>(calloc(1, sizeof(vc_device_t)));
    if (!device)
        return ERR_NO_MEMORY;

    mx_hid_fifo_init(&device->fifo);
    device->keymap = qwerty_map;
    char* keys = getenv("gfxconsole.keymap");
    if (keys) {
        if (!strcmp(keys, "qwerty")) {
            device->keymap = qwerty_map;
        } else if (!strcmp(keys, "dvorak")) {
            device->keymap = dvorak_map;
        } else {
            printf("gfxconsole: no such keymap '%s'\n", keys);
        }
    }

    device->font = vc_get_font();
    device->charw = device->font->width;
    device->charh = device->font->height;

    // init the status bar
    device->st_gfx = gfx_create_surface(NULL, hw_gfx->width, device->charh, hw_gfx->stride, hw_gfx->format, 0);
    if (!device->st_gfx)
        goto fail;

    size_t sz;  // Declare and initialize separately due to use of goto.
    sz = hw_gfx->pixelsize * hw_gfx->stride * hw_gfx->height;
    if ((mx_vmo_create(sz, 0, &device->gfx_vmo)) < 0)
        goto fail;

    uintptr_t ptr;
    if (mx_vmar_map(mx_vmar_root_self(), 0, device->gfx_vmo, 0, sz,
                    MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr) < 0) {
        mx_handle_close(device->gfx_vmo);
        goto fail;
    }

    // init the main surface
    device->gfx = gfx_create_surface((void*) ptr, hw_gfx->width, hw_gfx->height,
                                     hw_gfx->stride, hw_gfx->format, 0);
    if (!device->gfx)
        goto fail;
    device->hw_gfx = hw_gfx;

    vc_device_setup(device);
    vc_device_reset(device);

    *out_dev = device;
    return NO_ERROR;
fail:
    vc_device_free(device);
    return ERR_NO_MEMORY;
}

void vc_device_free(vc_device_t* device) {
    if (device->st_gfx)
        gfx_surface_destroy(device->st_gfx);
    if (device->gfx_vmo)
        mx_handle_close(device->gfx_vmo);
    if (device->gfx)
        free(device->gfx);
    free(device->text_buf);
    free(device->scrollback_buf);
    free(device);
}
