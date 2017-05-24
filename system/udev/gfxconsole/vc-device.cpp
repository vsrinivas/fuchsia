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

static mx_status_t vc_setup(vc_t* vc) {
    assert(vc->gfx);

    // calculate how many rows/columns we have
    vc->rows = vc->gfx->height / vc->charh;
    vc->columns = vc->gfx->width / vc->charw;
    vc->scrollback_rows_max = SCROLLBACK_ROWS;
    vc->scrollback_rows_count = 0;
    vc->scrollback_offset = 0;

    // allocate the text buffer
    vc->text_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, vc->rows * vc->columns * sizeof(vc_char_t)));
    if (!vc->text_buf)
        return ERR_NO_MEMORY;

    // allocate the scrollback buffer
    vc->scrollback_buf = reinterpret_cast<vc_char_t*>(
        calloc(1, vc->scrollback_rows_max * vc->columns * sizeof(vc_char_t)));
    if (!vc->scrollback_buf) {
        free(vc->text_buf);
        return ERR_NO_MEMORY;
    }

    // set up the default palette
    memcpy(&vc->palette, default_palette, sizeof(default_palette));
    vc->front_color = DEFAULT_FRONT_COLOR;
    vc->back_color = DEFAULT_BACK_COLOR;

    return NO_ERROR;
}

static void vc_invalidate(void* cookie, int x0, int y0, int w, int h) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);

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

static void vc_tc_invalidate(void* cookie, int x0, int y0,
                             int w, int h) TA_REQ(g_vc_lock) {
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
    if (!vc->hide_cursor) {
        // Clear the cursor from its old position.
        vc_invalidate(cookie, old_x, old_y, 1, 1);
        vc_invalidate_lines(vc, old_y, 1);

        // Display the cursor in its new position.
        vc_invalidate(cookie, vc->cursor_x, vc->cursor_y, 1, 1);
        vc_invalidate_lines(vc, vc->cursor_y, 1);
    }
}

static void vc_tc_push_scrollback_line(void* cookie, int y) TA_REQ(g_vc_lock) {
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
    vc_invalidate(vc, vc->cursor_x, vc->cursor_y, 1, 1);
    vc_invalidate_lines(vc, vc->cursor_y, 1);
}

static void vc_tc_copy_lines(void* cookie, int y_dest, int y_src,
                             int line_count) TA_REQ(g_vc_lock) {
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
    vc_set_cursor_hidden(vc, true);

    // The next two calls can be done in any order.
    tc_copy_lines(&vc->textcon, y_dest, y_src, line_count);
    gfx_copyrect(vc->gfx, 0, y_src * vc->charh,
                 vc->gfx->width, line_count * vc->charh,
                 0, y_dest * vc->charh);

    // Restore the cursor.
    vc_set_cursor_hidden(vc, old_hide_cursor);

    vc_write_status(vc);
    vc_gfx_invalidate_status(vc);
    vc_invalidate_lines(vc, 0, vc_rows(vc));
}

static void vc_tc_setparam(void* cookie, int param, uint8_t* arg,
                           size_t arglen) TA_REQ(g_vc_lock) {
    vc_t* vc = reinterpret_cast<vc_t*>(cookie);
    switch (param) {
    case TC_SET_TITLE:
        strncpy(vc->title, (char*)arg, sizeof(vc->title));
        vc->title[sizeof(vc->title) - 1] = '\0';
        vc_write_status(vc);
        vc_gfx_invalidate_status(vc);
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
    gfx_fillrect(vc->gfx, 0, 0, vc->gfx->width, vc->gfx->height,
                 palette_to_color(vc, vc->back_color));
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
    gfx_flush(vc->gfx);

    vc_gfx_invalidate_all(vc);
}

#define STATUS_FG 7
#define STATUS_BG 0

static void write_status_at(vc_t* vc, const char* str, unsigned offset) {
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
                gfx_putchar(vc->st_gfx, vc->font, c, idx++ * vc->charw, 0,
                            palette_to_color(vc, fg), palette_to_color(vc, bg));
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

void vc_write_status(vc_t* vc) {
    char str[512];

    // draw the tabs
    vc_get_status_line(str, sizeof(str));
    // TODO clean this up with textcon stuff
    gfx_fillrect(vc->st_gfx, 0, 0, vc->st_gfx->width, vc->st_gfx->height, palette_to_color(vc, STATUS_BG));
    write_status_at(vc, str, 0);

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
    write_status_at(vc, str, vc->columns - 8);

    gfx_flush(vc->st_gfx);
}

void vc_render(vc_t* vc) {
    vc_write_status(vc);
    vc_gfx_invalidate_all(vc);
}

void vc_invalidate_all_for_testing(vc_t* vc) {
    // This function is called from tests which don't use threading and so
    // don't need locking.  We claim the following lock just to keep
    // Clang's thread annotations checker happy.
    mxtl::AutoLock lock(&g_vc_lock);

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

static void vc_scroll_viewport_abs(vc_t* vc,
                                          int vpy) TA_REQ(g_vc_lock) {
    vpy = MIN(vpy, 0);
    vpy = MAX(vpy, -vc_get_scrollback_lines(vc));
    int diff = vpy - vc->viewport_y;
    if (diff == 0)
        return;
    int diff_abs = ABS(diff);
    vc->viewport_y = vpy;
    int rows = vc_rows(vc);
    if (diff_abs >= rows) {
        // We are scrolling the viewport by a large delta.  Invalidate all
        // of the visible area of the console.
        vc_invalidate(vc, 0, vpy, vc->columns, rows);
    } else {
        if (diff > 0) {
            gfx_copyrect(vc->gfx, 0, diff_abs * vc->charh,
                         vc->gfx->width, (rows - diff_abs) * vc->charh, 0, 0);
            vc_invalidate(vc, 0, vpy + rows - diff_abs, vc->columns,
                                 diff_abs);
        } else {
            gfx_copyrect(vc->gfx, 0, 0, vc->gfx->width,
                         (rows - diff_abs) * vc->charh, 0,
                         diff_abs * vc->charh);
            vc_invalidate(vc, 0, vpy, vc->columns, diff_abs);
        }
    }
    gfx_flush(vc->gfx);
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

mx_status_t vc_alloc(gfx_surface* test, int fd, vc_t** out_dev) {
    vc_t* vc =
        reinterpret_cast<vc_t*>(calloc(1, sizeof(vc_t)));
    if (!vc)
        return ERR_NO_MEMORY;
    vc->fd = -1;

    vc->keymap = qwerty_map;
    char* keys = getenv("gfxconsole.keymap");
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

#if BUILD_FOR_TEST
    // init the status bar
    vc->st_gfx = gfx_create_surface(NULL, test->width, vc->charh,
                                        test->stride, test->format, 0);
    if (!vc->st_gfx)
        goto fail;

    // init the main surface
    vc->gfx = gfx_create_surface(NULL, test->width, test->height,
                                     test->stride, test->format, 0);
    if (!vc->gfx)
        goto fail;

    vc->test_gfx = test;
#else
    ioctl_display_get_fb_t fb;
    size_t sz = 0;

    if (fd < 0) {
        goto fail;
    }
    if ((fd = openat(fd, "0", O_RDWR)) < 0) {
        printf("vc_alloc: cannot obtain fb driver instance\n");
        goto fail;
    }
    if (ioctl_display_get_fb(fd, &fb) != sizeof(fb)) {
        close(fd);
        printf("vc_alloc: cannot get fb from driver instance\n");
        goto fail;
    }
    vc->fd = fd;
    vc->gfx_vmo = fb.vmo;

    sz = fb.info.stride * fb.info.pixelsize * fb.info.height;

    uintptr_t ptr;
    if (mx_vmar_map(mx_vmar_root_self(), 0, vc->gfx_vmo, 0, sz,
                    MX_VM_FLAG_PERM_READ | MX_VM_FLAG_PERM_WRITE, &ptr) < 0) {
        goto fail;
    }

    // init the status bar
    vc->st_gfx = gfx_create_surface((void*) ptr, fb.info.width, vc->charh,
                                        fb.info.stride, fb.info.format, 0);
    if (!vc->st_gfx)
        goto fail;

    // init the main surface
    ptr += fb.info.stride * vc->charh * fb.info.pixelsize;
    vc->gfx = gfx_create_surface((void*) ptr, fb.info.width, fb.info.height - vc->charh,
                                     fb.info.stride, fb.info.format, 0);
    if (!vc->gfx)
        goto fail;
#endif

    vc_setup(vc);
    vc_reset(vc);

    *out_dev = vc;
    return NO_ERROR;
fail:
    vc_free(vc);
    return ERR_NO_MEMORY;
}

void vc_free(vc_t* vc) {
    //TODO: unmap framebuffer
    if (vc->fd >= 0) {
        close(vc->fd);
    }
    if (vc->st_gfx) {
        gfx_surface_destroy(vc->st_gfx);
    }
    if (vc->gfx_vmo) {
        mx_handle_close(vc->gfx_vmo);
    }
    if (vc->gfx) {
        free(vc->gfx);
    }
    free(vc->text_buf);
    free(vc->scrollback_buf);
    free(vc);
}
