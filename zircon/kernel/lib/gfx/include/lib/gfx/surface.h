// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_SURFACE_H_
#define ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_SURFACE_H_

#include <inttypes.h>
#include <lib/gfx-font/gfx-font.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zircon/compiler.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

typedef zx_pixel_format_t gfx_format;

namespace gfx {

// gfx library

#define MAX_ALPHA 255

// surface flags
#define GFX_FLAG_FREE_ON_DESTROY (1 << 0)  // free the ptr at destroy
#define GFX_FLAG_FLUSH_CPU_CACHE (1 << 1)  // do a cache flush during Flush

struct Context {
  void (*vlog)(const char* format, va_list v);

  void (*log)(const char* format, ...);
  void (*panic)(const char* format, ...);
  void (*flush_cache)(void* start, size_t len);
};

// TODO(fxbug.dev/96043): `class Surface`.
/**
 * @brief  Describe a graphics drawing surface
 *
 * The Surface object represents a framebuffer that can be rendered
 * to.  Elements include a pointer to the actual pixel memory, its size, its
 * layout, and pointers to basic drawing functions.
 *
 * @ingroup graphics
 */
struct Surface {
  void* ptr;
  const Context* ctx;
  uint32_t flags;
  uint32_t format;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t pixelsize;
  size_t len;
  uint32_t alpha;

  // function pointers
  uint32_t (*TranslateColor)(uint32_t input);
  void (*CopyRectangle)(Surface*, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                        uint32_t x2, uint32_t y2);
  void (*FillRectangle)(Surface*, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                        uint32_t color);
  void (*PutPixel)(Surface*, uint32_t x, uint32_t y, uint32_t color);
  void (*PutChar)(Surface*, const gfx_font_t*, uint32_t ch, uint32_t x, uint32_t y, uint32_t fg,
                  uint32_t bg);
  void (*Flush)(uint32_t starty, uint32_t endy);
};

// copy a rect from x,y with width x height to x2, y2
void CopyRectangle(Surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t x2, uint32_t y2);

// fill a rect within the surface with a color
void FillRectangle(Surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color);

// draw a pixel at x, y in the surface
void PutPixel(Surface* surface, uint32_t x, uint32_t y, uint32_t color);

// draw a character at x, y in the surface
void PutChar(Surface*, const gfx_font_t*, uint32_t ch, uint32_t x, uint32_t y, uint32_t fg,
             uint32_t bg);

// draw a single pixel line between x1,y1 and x2,y1
void DrawLine(Surface* surface, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2, uint32_t color);

// blend source surface to target surface
void Blend(Surface* target, Surface* source, uint32_t destx, uint32_t desty);

// blend an area from the source surface to the target surface
void Blend(Surface* target, Surface* source, uint32_t srcx, uint32_t srcy, uint32_t width,
           uint32_t height, uint32_t destx, uint32_t desty);

// copy entire lines from src to dst, which must be the same stride and pixel format
void CopyLines(Surface* dst, Surface* src, uint32_t srcy, uint32_t dsty, uint32_t height);

// ensure the surface is written back to memory and optionally backing store
void Flush(Surface* surface);

// flush a subset of the surface
void Flush(Surface* surface, uint32_t start, uint32_t end);

// clear the entire surface with a color
static inline void Clear(Surface* surface, uint32_t color) {
  surface->FillRectangle(surface, 0, 0, surface->width, surface->height, color);
  Flush(surface);
}

// surface setup
Surface* CreateSurfaceWithContext(void* ptr, const Context* ctx, uint32_t width, uint32_t height,
                                  uint32_t stride, uint32_t format, uint32_t flags);
zx_status_t InitSurface(Surface* surface, void* ptr, uint32_t width, uint32_t height,
                        uint32_t stride, uint32_t format, uint32_t flags);

// free the surface
// optionally frees the buffer if the free bit is set
void DestroySurface(Surface* surface);

}  // namespace gfx

#endif  // ZIRCON_KERNEL_LIB_GFX_INCLUDE_LIB_GFX_SURFACE_H_
