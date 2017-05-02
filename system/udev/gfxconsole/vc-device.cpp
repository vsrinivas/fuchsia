// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <magenta/process.h>
#include <mxtl/auto_lock.h>

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

    // calculate how many rows/columns we have
    dev->rows = dev->gfx->height / dev->charh;
    dev->columns = dev->gfx->width / dev->charw;
    dev->scrollback_rows_max = SCROLLBACK_ROWS;
    dev->scrollback_rows_count = 0;
    dev->scrollback_offset = 0;

    // allocate the text buffer
    dev->text_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, dev->rows * dev->columns * sizeof(vc_char_t)));
    if (!dev->text_buf)
        return ERR_NO_MEMORY;

    // allocate the scrollback buffer
    dev->scrollback_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, dev->scrollback_rows_max * dev->columns * sizeof(vc_char_t)));
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

    assert(h >= 0);
    int y1 = y0 + h;
    assert(y0 <= static_cast<int>(dev->rows));
    assert(y1 <= static_cast<int>(dev->rows));

    // Clip the y range so that we don't unnecessarily draw characters
    // outside the visible range, and so that we don't draw characters into
    // the bottom margin.
    int visible_y0 = dev->viewport_y;
    int visible_y1 = dev->viewport_y + vc_device_rows(dev);
    y0 = MAX(y0, visible_y0);
    y1 = MIN(y1, visible_y1);

    for (int y = y0; y < y1; y++) {
        if (y < 0) {
            // Scrollback row.
            vc_char_t* row = vc_device_get_scrollback_line_ptr(
                dev, y + dev->scrollback_rows_count);
            for (int x = x0; x < x0 + w; x++) {
                vc_gfx_draw_char(dev, row[x], x, y - dev->viewport_y,
                                 /* invert= */ false);
            }
        } else {
            // Row in the main console region (non-scrollback).
            vc_char_t* row = &dev->text_buf[y * dev->columns];
            for (int x = x0; x < x0 + w; x++) {
                // Check whether we should display the cursor at this
                // position.  Note that it's possible that the cursor is
                // outside the display area (dev->cursor_x ==
                // dev->columns).  In that case, we won't display the
                // cursor, even if there's a margin.  This matches
                // gnome-terminal.
                bool invert = (!dev->hide_cursor &&
                               static_cast<unsigned>(x) == dev->cursor_x &&
                               static_cast<unsigned>(y) == dev->cursor_y);
                vc_gfx_draw_char(dev, row[x], x, y - dev->viewport_y, invert);
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

static void vc_tc_invalidate(void* cookie, int x0, int y0,
                             int w, int h) TA_REQ(g_vc_lock) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
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

static void vc_tc_push_scrollback_line(void* cookie, int y) TA_REQ(g_vc_lock) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);

    unsigned dest_row;
    assert(dev->scrollback_rows_count <= dev->scrollback_rows_max);
    if (dev->scrollback_rows_count < dev->scrollback_rows_max) {
        // Add a row without dropping any existing rows.
        assert(dev->scrollback_offset == 0);
        dest_row = dev->scrollback_rows_count++;
    } else {
        // Add a row and drop an existing row.
        assert(dev->scrollback_offset < dev->scrollback_rows_max);
        dest_row = dev->scrollback_offset++;
        if (dev->scrollback_offset == dev->scrollback_rows_max)
            dev->scrollback_offset = 0;
    }
    vc_char_t* dst = &dev->scrollback_buf[dest_row * dev->columns];
    vc_char_t* src = &dev->text_buf[y * dev->columns];
    memcpy(dst, src, dev->columns * sizeof(vc_char_t));

    // If we're displaying only the main console region (and no
    // scrollback), then keep displaying that (i.e. don't modify
    // viewport_y).
    if (dev->viewport_y < 0) {
        // We are displaying some of the scrollback buffer.
        if (dev->viewport_y > -static_cast<int>(dev->scrollback_rows_max)) {
            // Scroll the viewport to continue displaying the same point in
            // the scrollback buffer.
            --dev->viewport_y;
        } else {
            // We were displaying the line at the top of the scrollback
            // buffer, but we dropped that line from the buffer.  We could
            // leave the display as it was (which is what gnome-terminal
            // does) and not scroll the display.  However, that causes
            // problems.  If the user later scrolls down, we won't
            // necessarily be able to display the lines below -- we might
            // have dropped those too.  So, instead, let's scroll the
            // display and remove the scrollback line that was lost.
            //
            // For simplicity, fall back to redrawing everything.
            vc_device_invalidate(dev, 0, -dev->scrollback_rows_max,
                                 dev->columns, vc_device_rows(dev));
            vc_device_render(dev);
        }
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
                             int line_count) TA_REQ(g_vc_lock) {
    vc_device_t* dev = reinterpret_cast<vc_device_t*>(cookie);
    if (dev->viewport_y < 0) {
        tc_copy_lines(&dev->textcon, y_dest, y_src, line_count);

        // The viewport is scrolled.  For simplicity, fall back to
        // redrawing all of the non-scrollback lines in this case.
        int rows = vc_device_rows(dev);
        vc_device_invalidate(dev, 0, 0, dev->columns, rows);
        vc_invalidate_lines(dev, 0, rows);
        return;
    }

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

static void vc_tc_setparam(void* cookie, int param, uint8_t* arg,
                           size_t arglen) TA_REQ(g_vc_lock) {
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
    dev->textcon.push_scrollback_line = vc_tc_push_scrollback_line;
    dev->textcon.copy_lines = vc_tc_copy_lines;
    dev->textcon.setparam = vc_tc_setparam;

    // fill textbuffer with blank characters
    size_t count = dev->rows * dev->columns;
    vc_char_t* ptr = dev->text_buf;
    while (count--) {
        *ptr++ = vc_char_make(' ', dev->front_color, dev->back_color);
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
    // This function is called from tests which don't use threading and so
    // don't need locking.  We claim the following lock just to keep
    // Clang's thread annotations checker happy.
    mxtl::AutoLock lock(&g_vc_lock);

    vc_device_clear_gfx(dev);
    int scrollback_lines = vc_device_get_scrollback_lines(dev);
    vc_device_invalidate(dev, 0, -scrollback_lines,
                         dev->columns, scrollback_lines + dev->rows);
}

int vc_device_get_scrollback_lines(vc_device_t* dev) {
    return dev->scrollback_rows_count;
}

vc_char_t* vc_device_get_scrollback_line_ptr(vc_device_t* dev, unsigned row) {
    assert(row < dev->scrollback_rows_count);
    row += dev->scrollback_offset;
    if (row >= dev->scrollback_rows_max)
        row -= dev->scrollback_rows_max;
    return &dev->scrollback_buf[row * dev->columns];
}

static void vc_device_scroll_viewport_abs(vc_device_t* dev,
                                          int vpy) TA_REQ(g_vc_lock) {
    vpy = MIN(vpy, 0);
    vpy = MAX(vpy, -vc_device_get_scrollback_lines(dev));
    int diff = vpy - dev->viewport_y;
    if (diff == 0)
        return;
    int diff_abs = ABS(diff);
    dev->viewport_y = vpy;
    int rows = vc_device_rows(dev);
    if (diff_abs >= rows) {
        // We are scrolling the viewport by a large delta.  Invalidate all
        // of the visible area of the console.
        vc_device_invalidate(dev, 0, vpy, dev->columns, rows);
    } else {
        if (diff > 0) {
            gfx_copyrect(dev->gfx, 0, diff_abs * dev->charh,
                         dev->gfx->width, (rows - diff_abs) * dev->charh, 0, 0);
            vc_device_invalidate(dev, 0, vpy + rows - diff_abs, dev->columns,
                                 diff_abs);
        } else {
            gfx_copyrect(dev->gfx, 0, 0, dev->gfx->width,
                         (rows - diff_abs) * dev->charh, 0,
                         diff_abs * dev->charh);
            vc_device_invalidate(dev, 0, vpy, dev->columns, diff_abs);
        }
    }
    gfx_flush(dev->gfx);
    vc_device_render(dev);
}

void vc_device_scroll_viewport(vc_device_t* dev, int dir) {
    vc_device_scroll_viewport_abs(dev, dev->viewport_y + dir);
}

void vc_device_scroll_viewport_top(vc_device_t* dev) {
    vc_device_scroll_viewport_abs(dev, INT_MIN);
}

void vc_device_scroll_viewport_bottom(vc_device_t* dev) {
    vc_device_scroll_viewport_abs(dev, 0);
}

void vc_device_set_fullscreen(vc_device_t* dev, bool fullscreen) {
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
    if (device->st_gfx) {
        gfx_surface_destroy(device->st_gfx);
    }
    if (device->gfx_vmo) {
        mx_handle_close(device->gfx_vmo);
    }
    if (device->gfx) {
        free(device->gfx);
    }
    free(device->text_buf);
    free(device->scrollback_buf);
    free(device);
}
