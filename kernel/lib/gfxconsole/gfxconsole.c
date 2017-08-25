// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2010, 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT


/**
 * @file
 * @brief  Manage graphics console
 *
 * This file contains functions to provide stdout to the graphics console.
 *
 * @ingroup graphics
 */

#include <debug.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/cmdline.h>
#include <lib/io.h>
#include <lib/gfx.h>
#include <lib/gfxconsole.h>
#include <dev/display.h>

#define TEXT_COLOR 0xffffffff
#define BACK_COLOR 0xff000000

#define CRASH_TEXT_COLOR 0xffffffff
#define CRASH_BACK_COLOR 0xffe000e0

/** @addtogroup graphics
 * @{
 */

/**
 * @brief  Represent state of graphics console
 */
static struct {
    // main surface to draw on
    gfx_surface *surface;
    // underlying hw surface, if different from above
    gfx_surface *hw_surface;

    // surface to do single line sub-region flushing with
    gfx_surface line;
    uint linestride;

    uint rows, columns;
    uint extray; // extra pixels left over if the rows doesn't fit precisely

    uint x, y;

    uint32_t front_color;
    uint32_t back_color;
} gfxconsole;

static void draw_char(char c, const struct gfx_font* font)
{
    gfx_putchar(gfxconsole.surface, font, c,
                gfxconsole.x * font->width, gfxconsole.y * font->height,
                gfxconsole.front_color, gfxconsole.back_color);
}

void gfxconsole_putpixel(unsigned x, unsigned y, unsigned color) {
    gfx_putpixel(gfxconsole.surface, x, y, color);
}

static const struct gfx_font* font = &font_9x16;

static bool gfxconsole_putc(char c)
{
    static enum { NORMAL, ESCAPE } state = NORMAL;
    static uint32_t p_num = 0;
    bool inval = 0;

    if (state == NORMAL) {
        switch (c) {
            case '\r':
                gfxconsole.x = 0;
                break;
            case '\n':
                gfxconsole.y++;
                inval = 1;
                break;
            case '\b':
                // back up one character unless we're at the left side
                if (gfxconsole.x > 0) {
                    gfxconsole.x--;
                }
                break;
            case '\t':
                gfxconsole.x = ROUNDUP(gfxconsole.x + 1, 8);
                break;
            case 0x1b:
                p_num = 0;
                state = ESCAPE;
                break;
            default:
                draw_char(c, font);
                gfxconsole.x++;
                break;
        }
    } else if (state == ESCAPE) {
        if (c >= '0' && c <= '9') {
            p_num = (p_num * 10) + (c - '0');
        } else if (c == 'D') {
            if (p_num <= gfxconsole.x)
                gfxconsole.x -= p_num;
            state = NORMAL;
        } else if (c == '[') {
            // eat this character
        } else {
            draw_char(c, font);
            gfxconsole.x++;
            state = NORMAL;
        }
    }

    if (gfxconsole.x >= gfxconsole.columns) {
        gfxconsole.x = 0;
        gfxconsole.y++;
        inval = 1;
    }
    if (gfxconsole.y >= gfxconsole.rows) {
        // scroll up
        gfx_copyrect(gfxconsole.surface, 0, font->height, gfxconsole.surface->width,
                     gfxconsole.surface->height - font->height - gfxconsole.extray, 0, 0);
        gfxconsole.y--;

        // clear the bottom line
        gfx_fillrect(gfxconsole.surface, 0, gfxconsole.surface->height - font->height - gfxconsole.extray,
                     gfxconsole.surface->width, font->height, gfxconsole.back_color);
        gfx_flush(gfxconsole.surface);
        inval = 1;
    }
    return inval;
}

static void gfxconsole_print_callback(print_callback_t *cb, const char *str, size_t len)
{
    int refresh_full_screen = 0;
    for (size_t i = 0; i < len; i++) {
        if (str[i] == '\n')
            gfxconsole_putc('\r');
        refresh_full_screen |= gfxconsole_putc(str[i]);
    }

    // blit from the software surface to the hardware
    if (gfxconsole.surface != gfxconsole.hw_surface) {
        if (refresh_full_screen) {
            gfx_surface_blend(gfxconsole.hw_surface, gfxconsole.surface, 0, 0);
        } else {
            // Only re-blit the active console line.
            // Since blend only works in whole surfaces, configure a sub-surface
            // to use as the blend source.
            gfxconsole.line.ptr = ((uint8_t*) gfxconsole.surface->ptr) +
                (gfxconsole.y * gfxconsole.linestride);
            gfx_surface_blend(gfxconsole.hw_surface, &gfxconsole.line,
                0, gfxconsole.y * font->height);
        }
        gfx_flush(gfxconsole.hw_surface);
    }
}

static print_callback_t cb = {
    .entry = {},
    .print = gfxconsole_print_callback,
    .context = NULL
};

static void gfxconsole_setup(gfx_surface *surface, gfx_surface *hw_surface)
{
    const char* fname = cmdline_get("gfxconsole.font");
    if (fname != NULL) {
        if (!strcmp(fname, "18x32")) {
            font = &font_18x32;
        } else if (!strcmp(fname, "9x16")) {
            font = &font_9x16;
        }
    }
    // set up the surface
    gfxconsole.surface = surface;
    gfxconsole.hw_surface = hw_surface;

    // set up line-height sub-surface, for line-only invalidation
    memcpy(&gfxconsole.line, surface, sizeof(*surface));
    gfxconsole.line.height = font->height;
    gfxconsole.linestride = surface->stride * surface->pixelsize * font->height;

    // calculate how many rows/columns we have
    gfxconsole.rows = surface->height / font->height;
    gfxconsole.columns = surface->width / font->width;
    gfxconsole.extray = surface->height - (gfxconsole.rows * font->height);

    dprintf(SPEW, "gfxconsole: rows %u, columns %u, extray %u\n", gfxconsole.rows,
            gfxconsole.columns, gfxconsole.extray);
}

static void gfxconsole_clear(bool crash_console)
{
    // start in the upper left
    gfxconsole.x = 0;
    gfxconsole.y = 0;

    if (crash_console) {
        gfxconsole.front_color = CRASH_TEXT_COLOR;
        gfxconsole.back_color = CRASH_BACK_COLOR;
    } else {
        gfxconsole.front_color = TEXT_COLOR;
        gfxconsole.back_color = BACK_COLOR;
    }

    // fill screen with back color
    gfx_fillrect(gfxconsole.surface, 0, 0, gfxconsole.surface->width, gfxconsole.surface->height,
                 gfxconsole.back_color);
    gfx_flush(gfxconsole.surface);
}

/**
 * @brief  Initialize graphics console on given drawing surface.
 *
 * The graphics console subsystem is initialized, and registered as
 * an output device for debug output.
 */
void gfxconsole_start(gfx_surface *surface, gfx_surface *hw_surface)
{
    DEBUG_ASSERT(gfxconsole.surface == NULL);

    gfxconsole_setup(surface, hw_surface);
    gfxconsole_clear(false);

    // register for debug callbacks
    register_print_callback(&cb);
}

static gfx_surface hw_surface;
static gfx_surface sw_surface;
static struct display_info dispinfo;

mx_status_t gfxconsole_display_get_info(struct display_info *info)
{
    if (gfxconsole.surface) {
        memcpy(info, &dispinfo, sizeof(*info));
        return 0;
    } else {
        return -1;
   }
}

/**
 * @brief  Initialize graphics console and bind to a display
 *
 * If the display was previously initialized, first it is shut down and
 * detached from the print callback.
 *
 * If the new display_info is NULL, nothing else is done, otherwise the
 * display is initialized against the provided display_info.
 *
 * If raw_sw_fb is non-NULL it is a memory large enough to be a backing
 * surface (stride * height * pixelsize) for the provided hardware display.
 * This is used for very early framebuffer init before the heap is alive.
 */
void gfxconsole_bind_display(struct display_info *info, void *raw_sw_fb) {
    static bool active = false;
    bool same_as_before = false;
    struct gfx_surface hw;

    if (active) {
        // on re-init or detach, we need to unhook from print callbacks
        active = false;
        unregister_print_callback(&cb);
    }
    if (info == NULL) {
        return;
    }

    if (gfx_init_surface_from_display(&hw, info)) {
        return;
    }
    if (info->flags & DISPLAY_FLAG_CRASH_FRAMEBUFFER) {
        // "bluescreen" path. no allocations allowed
        memcpy(&hw_surface, &hw, sizeof(hw));
        gfxconsole_setup(&hw_surface, &hw_surface);
        memcpy(&dispinfo, info, sizeof(*info));
        gfxconsole_clear(true);
        register_print_callback(&cb);
        active = true;
        return;
    }
    if ((hw.format == hw_surface.format) && (hw.width == hw_surface.width) &&
        (hw.height == hw_surface.height) && (hw.stride == hw_surface.stride) &&
        (hw.pixelsize == hw_surface.pixelsize)) {
        // we are binding to a new hw surface with the same properties
        // as the existing one
        same_as_before = true;
    } else {
        // we cannot re-use the sw backing surface, so destroy it
        if (sw_surface.ptr && (sw_surface.flags & GFX_FLAG_FREE_ON_DESTROY)) {
            free(sw_surface.ptr);
        }
        memset(&sw_surface, 0, sizeof(sw_surface));
    }
    memcpy(&hw_surface, &hw, sizeof(hw));

    gfx_surface *s = &hw_surface;
    if (info->flags & DISPLAY_FLAG_HW_FRAMEBUFFER) {
        if (!same_as_before) {
            // we can't re-use the existing sw_surface, create a new one
            if (gfx_init_surface(&sw_surface, raw_sw_fb, hw_surface.width,
                hw_surface.height, hw_surface.stride, hw_surface.format, 0)) {
                return;
            }
        }
        s = &sw_surface;
    } else {
        // for non-hw surfaces we're not using a backing surface
        // so we can't be the same as before and must fully init
        same_as_before = false;
    }

    gfxconsole_setup(s, &hw_surface);

    if (!same_as_before) {
        // on first init, or different-backing-buffer re-init
        // we clear and reset to x,y @ 0,0
        gfxconsole_clear(false);
    }

    memcpy(&dispinfo, info, sizeof(*info));
    register_print_callback(&cb);
    active = true;
}
