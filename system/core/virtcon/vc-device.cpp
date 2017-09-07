// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#include <magenta/device/display.h>
#include <magenta/process.h>
#include <magenta/syscalls.h>

#include <fbl/auto_lock.h>

#include "vc.h"

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

#define SPECIAL_FRONT_COLOR 0xf // white
#define SPECIAL_BACK_COLOR 0x4  // blue

#define SCROLLBACK_ROWS 1024 // TODO make configurable

#define ABS(val) (((val) >= 0) ? (val) : -(val))

// shared with vc-gfx.cpp
extern gfx_surface* vc_gfx;
extern gfx_surface* vc_tb_gfx;
extern const gfx_font* vc_font;

static mx_status_t vc_setup(vc_t* vc, bool special) {
    // calculate how many rows/columns we have
    vc->rows = vc_gfx->height / vc->charh;
    vc->columns = vc_gfx->width / vc->charw;
    vc->scrollback_rows_max = SCROLLBACK_ROWS;
    vc->scrollback_rows_count = 0;
    vc->scrollback_offset = 0;

    // allocate the text buffer
    vc->text_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, vc->rows * vc->columns * sizeof(vc_char_t)));
    if (!vc->text_buf)
        return MX_ERR_NO_MEMORY;

    // allocate the scrollback buffer
    vc->scrollback_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, vc->scrollback_rows_max * vc->columns * sizeof(vc_char_t)));
    if (!vc->scrollback_buf) {
        free(vc->text_buf);
        return MX_ERR_NO_MEMORY;
    }

    // set up the default palette
    memcpy(&vc->palette, default_palette, sizeof(default_palette));
    if (special) {
        vc->front_color = SPECIAL_FRONT_COLOR;
        vc->back_color = SPECIAL_BACK_COLOR;
    } else {
        vc->front_color = DEFAULT_FRONT_COLOR;
        vc->back_color = DEFAULT_BACK_COLOR;
    }

    return MX_OK;
}

static void vc_invalidate(void* cookie, int x0, int y0, int w, int h) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);

    if (!vc->active) {
        return;
    }

    assert(h >= 0);
    int y1 = y0 + h;
    assert(y0 <= static_cast<int>(vc->rows));
    assert(y1 <= static_cast<int>(vc->rows));

    // Clip the y range so that we don't unnecessarily draw characters
    // outside the visible range, and so that we don't draw characters into
    // the bottom margin.
    int visible_y0 = vc->viewport_y;
    int visible_y1 = vc->viewport_y + vc_rows(vc);
    y0 = MAX(y0, visible_y0);
    y1 = MIN(y1, visible_y1);

    for (int y = y0; y < y1; y++) {
        if (y < 0) {
            // Scrollback row.
            vc_char_t* row = vc_get_scrollback_line_ptr(
                vc, y + vc->scrollback_rows_count);
            for (int x = x0; x < x0 + w; x++) {
                vc_gfx_draw_char(vc, row[x], x, y - vc->viewport_y,
                                 /* invert= */ false);
            }
        } else {
            // Row in the main console region (non-scrollback).
            vc_char_t* row = &vc->text_buf[y * vc->columns];
            for (int x = x0; x < x0 + w; x++) {
                // Check whether we should display the cursor at this
                // position.  Note that it's possible that the cursor is
                // outside the display area (vc->cursor_x ==
                // vc->columns).  In that case, we won't display the
                // cursor, even if there's a margin.  This matches
                // gnome-terminal.
                bool invert = (!vc->hide_cursor &&
                               static_cast<unsigned>(x) == vc->cursor_x &&
                               static_cast<unsigned>(y) == vc->cursor_y);
                vc_gfx_draw_char(vc, row[x], x, y - vc->viewport_y, invert);
            }
        }
    }
}

// implement tc callbacks:

static inline void vc_invalidate_lines(vc_t* vc, int y, int h) {
    if (y < vc->invy0) {
        vc->invy0 = y;
    }
    y += h;
    if (y > vc->invy1) {
        vc->invy1 = y;
    }
}

static void vc_tc_invalidate(void* cookie, int x0, int y0, int w, int h){
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);
    vc_invalidate(cookie, x0, y0, w, h);
    vc_invalidate_lines(vc, y0, h);
}

static void vc_tc_movecursor(void* cookie, int x, int y) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);
    unsigned old_x = vc->cursor_x;
    unsigned old_y = vc->cursor_y;
    vc->cursor_x = x;
    vc->cursor_y = y;
    if (vc->active && !vc->hide_cursor) {
        // Clear the cursor from its old position.
        vc_invalidate(cookie, old_x, old_y, 1, 1);
        vc_invalidate_lines(vc, old_y, 1);

        // Display the cursor in its new position.
        vc_invalidate(cookie, vc->cursor_x, vc->cursor_y, 1, 1);
        vc_invalidate_lines(vc, vc->cursor_y, 1);
    }
}

static void vc_tc_push_scrollback_line(void* cookie, int y) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);

    unsigned dest_row;
    assert(vc->scrollback_rows_count <= vc->scrollback_rows_max);
    if (vc->scrollback_rows_count < vc->scrollback_rows_max) {
        // Add a row without dropping any existing rows.
        assert(vc->scrollback_offset == 0);
        dest_row = vc->scrollback_rows_count++;
    } else {
        // Add a row and drop an existing row.
        assert(vc->scrollback_offset < vc->scrollback_rows_max);
        dest_row = vc->scrollback_offset++;
        if (vc->scrollback_offset == vc->scrollback_rows_max)
            vc->scrollback_offset = 0;
    }
    vc_char_t* dst = &vc->scrollback_buf[dest_row * vc->columns];
    vc_char_t* src = &vc->text_buf[y * vc->columns];
    memcpy(dst, src, vc->columns * sizeof(vc_char_t));

    // If we're displaying only the main console region (and no
    // scrollback), then keep displaying that (i.e. don't modify
    // viewport_y).
    if (vc->viewport_y < 0) {
        // We are displaying some of the scrollback buffer.
        if (vc->viewport_y > -static_cast<int>(vc->scrollback_rows_max)) {
            // Scroll the viewport to continue displaying the same point in
            // the scrollback buffer.
            --vc->viewport_y;
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
            vc_invalidate(vc, 0, -vc->scrollback_rows_max,
                                 vc->columns, vc_rows(vc));
            vc_render(vc);
        }
    }
}

static void vc_set_cursor_hidden(vc_t* vc, bool hide) {
    if (vc->hide_cursor == hide)
        return;
    vc->hide_cursor = hide;
    if (vc->active) {
        vc_invalidate(vc, vc->cursor_x, vc->cursor_y, 1, 1);
        vc_invalidate_lines(vc, vc->cursor_y, 1);
    }
}

static void vc_tc_copy_lines(void* cookie, int y_dest, int y_src, int line_count) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);

    if (vc->viewport_y < 0) {
        tc_copy_lines(&vc->textcon, y_dest, y_src, line_count);

        // The viewport is scrolled.  For simplicity, fall back to
        // redrawing all of the non-scrollback lines in this case.
        int rows = vc_rows(vc);
        vc_invalidate(vc, 0, 0, vc->columns, rows);
        vc_invalidate_lines(vc, 0, rows);
        return;
    }

    // Remove the cursor from the display before copying the lines on
    // screen, otherwise we might be copying a rendering of the cursor to a
    // position where the cursor isn't.  This must be done before the
    // tc_copy_lines() call, otherwise we might render the wrong character.
    bool old_hide_cursor = vc->hide_cursor;
    if (vc->active) {
        vc_set_cursor_hidden(vc, true);
    }

    // The next two calls can be done in any order.
    tc_copy_lines(&vc->textcon, y_dest, y_src, line_count);

    if (vc->active) {
        gfx_copyrect(vc_gfx, 0, y_src * vc->charh,
                     vc_gfx->width, line_count * vc->charh,
                     0, y_dest * vc->charh);

        // Restore the cursor.
        vc_set_cursor_hidden(vc, old_hide_cursor);

        vc_status_update();
        vc_gfx_invalidate_status();
        vc_invalidate_lines(vc, 0, vc_rows(vc));
    }
}

static void vc_tc_setparam(void* cookie, int param, uint8_t* arg, size_t arglen) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);
    switch (param) {
    case TC_SET_TITLE:
        strncpy(vc->title, (char*)arg, sizeof(vc->title));
        vc->title[sizeof(vc->title) - 1] = '\0';
        vc_status_update();
        vc_gfx_invalidate_status();
        break;
    case TC_SHOW_CURSOR:
        vc_set_cursor_hidden(vc, false);
        break;
    case TC_HIDE_CURSOR:
        vc_set_cursor_hidden(vc, true);
        break;
    default:; // nothing
    }
}

static void vc_clear_gfx(vc_t* vc) {
    // Fill display with background color
    if (vc->active) {
        gfx_fillrect(vc_gfx, 0, 0, vc_gfx->width, vc_gfx->height,
                     palette_to_color(vc, vc->back_color));
    }
}

static void vc_reset(vc_t* vc) {
    // reset the cursor
    vc->cursor_x = 0;
    vc->cursor_y = 0;
    // reset the viewport position
    vc->viewport_y = 0;

    tc_init(&vc->textcon, vc->columns, vc_rows(vc), vc->text_buf, vc->front_color, vc->back_color);
    vc->textcon.cookie = vc;
    vc->textcon.invalidate = vc_tc_invalidate;
    vc->textcon.movecursor = vc_tc_movecursor;
    vc->textcon.push_scrollback_line = vc_tc_push_scrollback_line;
    vc->textcon.copy_lines = vc_tc_copy_lines;
    vc->textcon.setparam = vc_tc_setparam;

    // fill textbuffer with blank characters
    size_t count = vc->rows * vc->columns;
    vc_char_t* ptr = vc->text_buf;
    while (count--) {
        *ptr++ = vc_char_make(' ', vc->front_color, vc->back_color);
    }

    vc_clear_gfx(vc);
    vc_gfx_invalidate_all(vc);
}


void vc_status_clear() {
    gfx_fillrect(vc_tb_gfx, 0, 0,
                 vc_tb_gfx->width, vc_tb_gfx->height,
                 default_palette[STATUS_COLOR_BG]);
}

void vc_status_write(int x, unsigned color, const char* text) {
    char c;
    unsigned fg = default_palette[color];
    unsigned bg = default_palette[STATUS_COLOR_BG];

    x *= vc_font->width;
    while ((c = *text++) != 0) {
        gfx_putchar(vc_tb_gfx, vc_font, c, x, 0, fg, bg);
        x += vc_font->width;
    }
}

void vc_render(vc_t* vc) {
    if (vc->active) {
        vc_status_update();
        vc_gfx_invalidate_all(vc);
    }
}

void vc_full_repaint(vc_t* vc) {
    vc_clear_gfx(vc);
    int scrollback_lines = vc_get_scrollback_lines(vc);
    vc_invalidate(vc, 0, -scrollback_lines,
                         vc->columns, scrollback_lines + vc->rows);
}

int vc_get_scrollback_lines(vc_t* vc) {
    return vc->scrollback_rows_count;
}

vc_char_t* vc_get_scrollback_line_ptr(vc_t* vc, unsigned row) {
    assert(row < vc->scrollback_rows_count);
    row += vc->scrollback_offset;
    if (row >= vc->scrollback_rows_max)
        row -= vc->scrollback_rows_max;
    return &vc->scrollback_buf[row * vc->columns];
}

static void vc_scroll_viewport_abs(vc_t* vc, int vpy) {
    vpy = MIN(vpy, 0);
    vpy = MAX(vpy, -vc_get_scrollback_lines(vc));
    int diff = vpy - vc->viewport_y;
    if (diff == 0)
        return;
    int diff_abs = ABS(diff);
    vc->viewport_y = vpy;
    int rows = vc_rows(vc);
    if (!vc->active) {
        return;
    }
    if (diff_abs >= rows) {
        // We are scrolling the viewport by a large delta.  Invalidate all
        // of the visible area of the console.
        vc_invalidate(vc, 0, vpy, vc->columns, rows);
    } else {
        if (diff > 0) {
            gfx_copyrect(vc_gfx, 0, diff_abs * vc->charh,
                         vc_gfx->width, (rows - diff_abs) * vc->charh, 0, 0);
            vc_invalidate(vc, 0, vpy + rows - diff_abs, vc->columns,
                                 diff_abs);
        } else {
            gfx_copyrect(vc_gfx, 0, 0, vc_gfx->width,
                         (rows - diff_abs) * vc->charh, 0,
                         diff_abs * vc->charh);
            vc_invalidate(vc, 0, vpy, vc->columns, diff_abs);
        }
    }
    vc_render(vc);
}

void vc_scroll_viewport(vc_t* vc, int dir) {
    vc_scroll_viewport_abs(vc, vc->viewport_y + dir);
}

void vc_scroll_viewport_top(vc_t* vc) {
    vc_scroll_viewport_abs(vc, INT_MIN);
}

void vc_scroll_viewport_bottom(vc_t* vc) {
    vc_scroll_viewport_abs(vc, 0);
}

void vc_set_fullscreen(vc_t* vc, bool fullscreen) {
    unsigned flags;
    if (fullscreen) {
        flags = vc->flags | VC_FLAG_FULLSCREEN;
    } else {
        flags = vc->flags & ~VC_FLAG_FULLSCREEN;
    }
    if (flags != vc->flags) {
        vc->flags = flags;
        tc_seth(&vc->textcon, vc_rows(vc));
    }
    vc_render(vc);
}

const gfx_font* vc_get_font() {
    char* fname = getenv("virtcon.font");
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

mx_status_t vc_alloc(vc_t** out, bool special) {
    vc_t* vc =
        reinterpret_cast<vc_t*>(calloc(1, sizeof(vc_t)));
    if (!vc) {
        return MX_ERR_NO_MEMORY;
    }
    vc->fd = -1;

    vc->keymap = qwerty_map;
    char* keys = getenv("virtcon.keymap");
    if (keys) {
        if (!strcmp(keys, "qwerty")) {
            vc->keymap = qwerty_map;
        } else if (!strcmp(keys, "dvorak")) {
            vc->keymap = dvorak_map;
        } else {
            printf("gfxconsole: no such keymap '%s'\n", keys);
        }
    }

    vc->font = vc_get_font();
    vc->charw = vc->font->width;
    vc->charh = vc->font->height;

    vc_setup(vc, special);
    vc_reset(vc);

    *out = vc;
    return MX_OK;
}

void vc_free(vc_t* vc) {
    if (vc->fd >= 0) {
        close(vc->fd);
    }
    free(vc->text_buf);
    free(vc->scrollback_buf);
    free(vc);
}

