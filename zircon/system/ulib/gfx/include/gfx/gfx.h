// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zircon/compiler.h>
#include <zircon/types.h>
#include <zircon/pixelformat.h>

__BEGIN_CDECLS

// gfx library

#define MAX_ALPHA 255

// surface flags
#define GFX_FLAG_FREE_ON_DESTROY (1 << 0) // free the ptr at destroy
#define GFX_FLAG_FLUSH_CPU_CACHE (1 << 1) // do a cache flush during gfx_flush

typedef struct gfx_surface gfx_surface;
typedef struct gfx_font gfx_font;

/**
 * @brief  Describe a graphics drawing surface
 *
 * The gfx_surface object represents a framebuffer that can be rendered
 * to.  Elements include a pointer to the actual pixel memory, its size, its
 * layout, and pointers to basic drawing functions.
 *
 * @ingroup graphics
 */
struct gfx_surface {
    void* ptr;
    uint32_t flags;
    uint32_t format;
    unsigned width;
    unsigned height;
    unsigned stride;
    unsigned pixelsize;
    size_t len;
    unsigned alpha;

    // function pointers
    uint32_t (*translate_color)(uint32_t input);
    void (*copyrect)(gfx_surface*, unsigned x, unsigned y, unsigned width, unsigned height, unsigned x2, unsigned y2);
    void (*fillrect)(gfx_surface*, unsigned x, unsigned y, unsigned width, unsigned height, unsigned color);
    void (*putpixel)(gfx_surface*, unsigned x, unsigned y, unsigned color);
    void (*putchar)(gfx_surface*, const gfx_font*, unsigned ch, unsigned x, unsigned y, unsigned fg, unsigned bg);
    void (*flush)(unsigned starty, unsigned endy);
};

struct gfx_font {
    const uint16_t* data;
    unsigned width;
    unsigned height;
};

// copy a rect from x,y with width x height to x2, y2
void gfx_copyrect(gfx_surface* surface, unsigned x, unsigned y, unsigned width, unsigned height, unsigned x2, unsigned y2);

// fill a rect within the surface with a color
void gfx_fillrect(gfx_surface* surface, unsigned x, unsigned y, unsigned width, unsigned height, unsigned color);

// draw a pixel at x, y in the surface
void gfx_putpixel(gfx_surface* surface, unsigned x, unsigned y, unsigned color);

// draw a character at x, y in the surface
void gfx_putchar(gfx_surface*, const gfx_font*, unsigned ch, unsigned x, unsigned y, unsigned fg, unsigned bg);

// draw a single pixel line between x1,y1 and x2,y1
void gfx_line(gfx_surface* surface, unsigned x1, unsigned y1, unsigned x2, unsigned y2, unsigned color);

// blend source surface to target surface
void gfx_surface_blend(struct gfx_surface* target, struct gfx_surface* source, unsigned destx, unsigned desty);

// blend an area from the source surface to the target surface
void gfx_blend(struct gfx_surface* target, struct gfx_surface* source, unsigned srcx, unsigned srcy, unsigned width, unsigned height, unsigned destx, unsigned desty);

// copy entire lines from src to dst, which must be the same stride and pixel format
void gfx_copylines(gfx_surface* dst, gfx_surface* src, unsigned srcy, unsigned dsty, unsigned height);

// ensure the surface is written back to memory and optionally backing store
void gfx_flush(struct gfx_surface* surface);

// flush a subset of the surface
void gfx_flush_rows(struct gfx_surface* surface, unsigned start, unsigned end);

// clear the entire surface with a color
static inline void gfx_clear(gfx_surface* surface, unsigned color) {
    surface->fillrect(surface, 0, 0, surface->width, surface->height, color);
    gfx_flush(surface);
}

// surface setup
gfx_surface* gfx_create_surface(void* ptr, unsigned width, unsigned height, unsigned stride, unsigned format, uint32_t flags);
zx_status_t gfx_init_surface(gfx_surface* surface, void* ptr, unsigned width, unsigned height, unsigned stride, unsigned format, uint32_t flags);

// free the surface
// optionally frees the buffer if the free bit is set
void gfx_surface_destroy(struct gfx_surface* surface);

// utility routine to fill the display with a little moire pattern
void gfx_draw_pattern(void);

extern const gfx_font font9x16;
extern const gfx_font font18x32;

__END_CDECLS
