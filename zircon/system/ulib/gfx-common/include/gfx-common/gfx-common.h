// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GFX_COMMON_GFX_COMMON_H_
#define GFX_COMMON_GFX_COMMON_H_

#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

#include <gfx-common/gfx-font.h>

__BEGIN_CDECLS

typedef zx_pixel_format_t gfx_format;

// gfx library

#define MAX_ALPHA 255

// surface flags
#define GFX_FLAG_FREE_ON_DESTROY (1 << 0)  // free the ptr at destroy
#define GFX_FLAG_FLUSH_CPU_CACHE (1 << 1)  // do a cache flush during gfx_flush

typedef struct gfx_context gfx_context;
typedef struct gfx_surface gfx_surface;

struct gfx_context {
  void (*vlog)(const char* format, va_list v);

  void (*log)(const char* format, ...);
  void (*panic)(const char* format, ...);
  void (*flush_cache)(void* start, size_t len);
};

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
  const gfx_context* ctx;
  uint32_t flags;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t pixelsize;
  size_t len;
  uint32_t alpha;

  // function pointers
  uint32_t (*translate_color)(uint32_t input);
  void (*copyrect)(gfx_surface*, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t x2, uint32_t y2);
  void (*fillrect)(gfx_surface*, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color);
  void (*putpixel)(gfx_surface*, uint32_t x, uint32_t y, uint32_t color);
  void (*putchar)(gfx_surface*, const gfx_font*, uint32_t ch, uint32_t x, uint32_t y, uint32_t fg,
                  uint32_t bg);
  void (*flush)(uint32_t starty, uint32_t endy);
};

// copy a rect from x,y with width x height to x2, y2
void gfx_copyrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t x2, uint32_t y2);

// fill a rect within the surface with a color
void gfx_fillrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color);

// draw a pixel at x, y in the surface
void gfx_putpixel(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t color);

// draw a character at x, y in the surface
void gfx_putchar(gfx_surface*, const gfx_font*, uint32_t ch, uint32_t x, uint32_t y, uint32_t fg,
                 uint32_t bg);

// draw a single pixel line between x1,y1 and x2,y1
void gfx_line(gfx_surface* surface, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
              uint32_t color);

// blend source surface to target surface
void gfx_surface_blend(struct gfx_surface* target, struct gfx_surface* source, uint32_t destx,
                       uint32_t desty);

// blend an area from the source surface to the target surface
void gfx_blend(struct gfx_surface* target, struct gfx_surface* source, uint32_t srcx, uint32_t srcy,
               uint32_t width, uint32_t height, uint32_t destx, uint32_t desty);

// copy entire lines from src to dst, which must be the same stride and pixel format
void gfx_copylines(gfx_surface* dst, gfx_surface* src, uint32_t srcy, uint32_t dsty,
                   uint32_t height);

// ensure the surface is written back to memory and optionally backing store
void gfx_flush(struct gfx_surface* surface);

// flush a subset of the surface
void gfx_flush_rows(struct gfx_surface* surface, uint32_t start, uint32_t end);

// clear the entire surface with a color
static inline void gfx_clear(gfx_surface* surface, uint32_t color) {
  surface->fillrect(surface, 0, 0, surface->width, surface->height, color);
  gfx_flush(surface);
}

// surface setup
gfx_surface* gfx_create_surface_with_context(void* ptr, const gfx_context* ctx, uint32_t width,
                                             uint32_t height, uint32_t stride, uint32_t format,
                                             uint32_t flags);
zx_status_t gfx_init_surface(gfx_surface* surface, void* ptr, uint32_t width, uint32_t height,
                             uint32_t stride, uint32_t format, uint32_t flags);

// free the surface
// optionally frees the buffer if the free bit is set
void gfx_surface_destroy(struct gfx_surface* surface);

// utility routine to fill the display with a little moire pattern
void gfx_draw_pattern(void);

__END_CDECLS

#endif  // GFX_COMMON_GFX_COMMON_H_
