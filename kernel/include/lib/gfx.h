// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2010 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#pragma once

#include <stdbool.h>
#include <sys/types.h>
#include <inttypes.h>

// gfx library

// different graphics formats
typedef enum {
    GFX_FORMAT_NONE,
    GFX_FORMAT_RGB_565,
    GFX_FORMAT_RGB_332,
    GFX_FORMAT_RGB_2220,
    GFX_FORMAT_ARGB_8888,
    GFX_FORMAT_RGB_x888,
    GFX_FORMAT_MONO,

    GFX_FORMAT_MAX
} gfx_format;

#define MAX_ALPHA 255

// surface flags
#define GFX_FLAG_FREE_ON_DESTROY (1<<0) // free the ptr at destroy
#define GFX_FLAG_FLUSH_CPU_CACHE (1<<1) // do a cache flush during gfx_flush

typedef struct gfx_font {
    const uint16_t* data;
    unsigned width;
    unsigned height;
} gfx_font;

/**
 * @brief  Describe a graphics drawing surface
 *
 * The gfx_surface object represents a framebuffer that can be rendered
 * to.  Elements include a pointer to the actual pixel memory, its size, its
 * layout, and pointers to basic drawing functions.
 *
 * @ingroup graphics
 */
typedef struct gfx_surface {
    void *ptr;
    uint32_t flags;
    gfx_format format;
    uint width;
    uint height;
    uint stride;
    uint pixelsize;
    size_t len;
    uint alpha;

    // function pointers
    uint32_t (*translate_color)(uint32_t input);
    void (*copyrect)(struct gfx_surface *, uint x, uint y, uint width, uint height, uint x2, uint y2);
    void (*fillrect)(struct gfx_surface *, uint x, uint y, uint width, uint height, uint color);
    void (*putpixel)(struct gfx_surface *, uint x, uint y, uint color);
    void (*putchar)(struct gfx_surface*, const struct gfx_font*,
                    uint ch, uint x, uint y, uint fg, uint bg);
    void (*flush)(uint starty, uint endy);
} gfx_surface;

extern const struct gfx_font font_9x16;
extern const struct gfx_font font_18x32;

// copy a rect from x,y with width x height to x2, y2
void gfx_copyrect(gfx_surface *surface, uint x, uint y, uint width, uint height, uint x2, uint y2);

// fill a rect within the surface with a color
void gfx_fillrect(gfx_surface *surface, uint x, uint y, uint width, uint height, uint color);

// draw a pixel at x, y in the surface
void gfx_putpixel(gfx_surface *surface, uint x, uint y, uint color);

// draw a single pixel line between x1,y1 and x2,y1
void gfx_line(gfx_surface *surface, uint x1, uint y1, uint x2, uint y2, uint color);

// blend between two surfaces
void gfx_surface_blend(struct gfx_surface *target, struct gfx_surface *source, uint destx, uint desty);

// ensure the surface is written back to memory and optionally backing store
void gfx_flush(struct gfx_surface *surface);

// flush a subset of the surface
void gfx_flush_rows(struct gfx_surface *surface, uint start, uint end);

void gfx_putchar(struct gfx_surface *surface, const struct gfx_font* font,
                 uint ch, uint x, uint y, uint fg, uint bg);

// clear the entire surface with a color
static inline void gfx_clear(gfx_surface *surface, uint color)
{
    surface->fillrect(surface, 0, 0, surface->width, surface->height, color);
    gfx_flush(surface);
}

// surface setup
gfx_surface *gfx_create_surface(void *ptr, uint width, uint height, uint stride, gfx_format format, uint32_t flags);
status_t gfx_init_surface(gfx_surface *surface, void *ptr, uint width, uint height, uint stride, gfx_format format, uint32_t flags);

// utility routine to make a surface out of a display info
struct display_info;
gfx_surface *gfx_create_surface_from_display(struct display_info *);
status_t gfx_init_surface_from_display(gfx_surface *surface, struct display_info *);

// free the surface
// optionally frees the buffer if the free bit is set
void gfx_surface_destroy(struct gfx_surface *surface);

// utility routine to fill the display with a little moire pattern
void gfx_draw_pattern(void);

