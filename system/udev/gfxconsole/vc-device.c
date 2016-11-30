// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <assert.h>
#include <ddk/protocol/console.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>

#define VCDEBUG 0

#include "vc.h"
#include "vcdebug.h"

static uint32_t default_palette[] = {
    0xff000000, // black
    0xff0000aa, // blue
    0xff00aa00, // green
    0xff00aaaa, // cyan
    0xffaa0000, // red
    0xffaa00aa, // magenta
    0xffaa5500, // brown
    0xffaaaaaa, // grey
    0xff555555, // dark grey
    0xff5555ff, // bright blue
    0xff55ff55, // bright green
    0xff55ffff, // bright cyan
    0xffff5555, // bright red
    0xffff55ff, // bright magenta
    0xffffff55, // yellow
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
    dev->text_buf = calloc(1, dev->rows * dev->columns * sizeof(vc_char_t));
    if (!dev->text_buf)
        return ERR_NO_MEMORY;

    // allocate the scrollback buffer
    dev->scrollback_buf = calloc(1, dev->scrollback_rows * dev->columns * sizeof(vc_char_t));
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
    vc_device_t* dev = cookie;
    for (int y = y0; y < y0 + h; y++) {
        int sc = 0;
        if (y < 0) {
            sc = dev->sc_t + y;
            if (sc < 0)
                sc += dev->scrollback_rows;
        }
        for (int x = x0; x < x0 + w; x++) {
            if (y < 0) {
                vc_gfx_draw_char(dev, dev->scrollback_buf[x + sc * dev->columns], x, y - dev->vpy);
            } else {
                vc_gfx_draw_char(dev, dev->text_buf[x + y * dev->columns], x, y - dev->vpy);
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
    vc_device_t* dev = cookie;
    if (dev->flags & VC_FLAG_RESETSCROLL) {
        dev->flags &= ~VC_FLAG_RESETSCROLL;
        vc_device_scroll_viewport(dev, -dev->vpy);
    }
    if (dev->vpy < 0)
        return;
    vc_device_invalidate(cookie, x0, y0, w, h);
    vc_invalidate_lines(dev, y0, h);
}

static void vc_tc_movecursor(void* cookie, int x, int y) {
    vc_device_t* dev = cookie;
    if (!dev->hide_cursor) {
        vc_device_invalidate(cookie, dev->x, dev->y, 1, 1);
        vc_invalidate_lines(dev, dev->y, 1);
        gfx_fillrect(dev->gfx, x * dev->charw, y * dev->charh, dev->charw, dev->charh,
                     palette_to_color(dev, dev->front_color));
        vc_invalidate_lines(dev, y, 1);
    }
    dev->x = x;
    dev->y = y;
}

static void vc_tc_pushline(void* cookie, int y) {
    vc_device_t* dev = cookie;
    vc_char_t* dst = &dev->scrollback_buf[dev->sc_t * dev->columns];
    vc_char_t* src = &dev->text_buf[y * dev->columns];
    memcpy(dst, src, dev->columns * sizeof(vc_char_t));
    dev->sc_t += 1;
    if (dev->vpy < 0)
        dev->vpy -= 1;
    if (dev->sc_t >= dev->scrollback_rows) {
        dev->sc_t -= dev->scrollback_rows;
        if (dev->sc_t >= dev->sc_h)
            dev->sc_h = dev->sc_t + 1;
    }
}

// positive = up, negative = down
// textbuf must be updated before calling scroll
static void vc_tc_scroll(void* cookie, int y0, int y1, int dir) {
    vc_device_t* dev = cookie;
    if (dev->vpy < 0)
        return;
    // invalidate the cursor before copying
    vc_device_invalidate(cookie, dev->x, dev->y, 1, 1);
    int delta = ABS(dir);
    if (dir > 0) {
        gfx_copyrect(dev->gfx, 0, (y0 + delta) * dev->charh,
                     dev->gfx->width, (y1 - y0 - delta) * dev->charh, 0, y0);
        vc_device_invalidate(cookie, 0, y1 - delta, dev->columns, delta);
    } else {
        gfx_copyrect(dev->gfx, 0, y0, dev->gfx->width, (y1 - y0 - delta) * dev->charh,
                     0, (y0 + delta) * dev->charh);
        vc_device_invalidate(cookie, 0, y0, dev->columns, delta);
    }
    vc_device_write_status(dev);
    vc_gfx_invalidate_status(dev);
    vc_invalidate_lines(dev, 0, vc_device_rows(dev));
}

static void vc_tc_setparam(void* cookie, int param, uint8_t* arg, size_t arglen) {
    vc_device_t* dev = cookie;
    switch (param) {
    case TC_SET_TITLE:
        strncpy(dev->title, (char*)arg, sizeof(dev->title));
        dev->title[sizeof(dev->title) - 1] = '\0';
        vc_device_write_status(dev);
        vc_gfx_invalidate_status(dev);
        break;
    case TC_SHOW_CURSOR:
        if (dev->hide_cursor) {
            dev->hide_cursor = false;
            vc_tc_movecursor(dev, dev->x, dev->y);
            gfx_fillrect(dev->gfx, dev->x * dev->charw, dev->y * dev->charh, dev->charw, dev->charh,
                         palette_to_color(dev, dev->front_color));
            vc_invalidate_lines(dev, dev->y, 1);
        }
        break;
    case TC_HIDE_CURSOR:
        if (!dev->hide_cursor) {
            dev->hide_cursor = true;
            vc_device_invalidate(cookie, dev->x, dev->y, 1, 1);
            vc_invalidate_lines(dev, dev->y, 1);
        }
    default:; // nothing
    }
}

static void vc_device_reset(vc_device_t* dev) {
    // reset the cursor
    dev->x = 0;
    dev->y = 0;
    // reset the viewport position
    dev->vpy = 0;

    tc_init(&dev->textcon, dev->columns, vc_device_rows(dev), dev->text_buf, dev->front_color, dev->back_color);
    dev->textcon.cookie = dev;
    dev->textcon.invalidate = vc_tc_invalidate;
    dev->textcon.movecursor = vc_tc_movecursor;
    dev->textcon.pushline = vc_tc_pushline;
    dev->textcon.scroll = vc_tc_scroll;
    dev->textcon.setparam = vc_tc_setparam;

    // fill textbuffer with blank characters
    size_t count = dev->rows * dev->columns;
    vc_char_t* ptr = dev->text_buf;
    while (count--) {
        *ptr++ = CHARVAL(' ', dev->front_color, dev->back_color);
    }

    // fill screen with back color
    gfx_fillrect(dev->gfx, 0, 0, dev->gfx->width, dev->gfx->height, palette_to_color(dev, dev->back_color));
    gfx_flush(dev->gfx);

    vc_gfx_invalidate_all(dev);
}

#define STATUS_FG 7
#define STATUS_BG 0

static void write_status_at(vc_device_t* dev, const char* str, unsigned offset) {
    static enum { NORMAL,
                  ESCAPE } state = NORMAL;
    int fg = STATUS_FG;
    int bg = STATUS_BG;
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
                    fg = p_num - 30;
                } else if (p_num >= 40 && p_num <= 47) {
                    bg = p_num - 40;
                } else if (p_num == 1 && fg <= 0x7) {
                    fg += 8;
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
            snprintf(str, sizeof(str), "\033[36m\033[1mc %d%%", info.pct);
            break;
        case NOT_CHARGING:
            if (info.pct <= 20) {
                snprintf(str, sizeof(str), "\033[34m\033[1m%d%%", info.pct);
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

int vc_device_get_scrollback_lines(vc_device_t* dev) {
    return dev->sc_t >= dev->sc_h ? dev->sc_t - dev->sc_h : dev->scrollback_rows - 1;
}

void vc_device_scroll_viewport(vc_device_t* dev, int dir) {
    int vpy = MAX(MIN(dev->vpy + dir, 0), -vc_device_get_scrollback_lines(dev));
    int delta = ABS(dev->vpy - vpy);
    if (delta == 0)
        return;
    dev->vpy = vpy;
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

mx_status_t vc_device_alloc(gfx_surface* hw_gfx, vc_device_t** out_dev) {
    vc_device_t* device = calloc(1, sizeof(vc_device_t));
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

    device->font = &font9x16;
    char* fname = getenv("gfxconsole.font");
    if (fname) {
        if (!strcmp(fname, "9x16")) {
            device->font = &font9x16;
        } else if (!strcmp(fname, "18x32")) {
            device->font = &font18x32;
        } else {
            printf("gfxconsole: no such font '%s'\n", fname);
        }
    }

    device->charw = device->font->width;
    device->charh = device->font->height;

    // init the status bar
    device->st_gfx = gfx_create_surface(NULL, hw_gfx->width, device->charh, hw_gfx->stride, hw_gfx->format, 0);
    if (!device->st_gfx)
        goto fail;

    size_t sz = hw_gfx->pixelsize * hw_gfx->stride * hw_gfx->height;
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
    free(device);
}
