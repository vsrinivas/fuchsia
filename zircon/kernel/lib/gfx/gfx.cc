// Copyright 2016 The Fuchsia Authors
// Copyright (c) 2008-2010, 2015 Travis Geiselbrecht
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#include <debug.h>
#include <lib/gfx.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <trace.h>
#include <zircon/errors.h>
#include <zircon/types.h>

#include <arch/ops.h>
#include <dev/display.h>

#define LOCAL_TRACE 0

static void kernel_gfx_log(const char* format, ...) {
  if (LOCAL_TRACE) {
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
  }
}

static void kernel_gfx_flush_cache(void* ptr, size_t size) {
  arch_clean_cache_range(reinterpret_cast<vaddr_t>(ptr), size);
}

static const gfx_context g_kernel_ctx = {
    .log = kernel_gfx_log,
    .panic = panic,
    .flush_cache = kernel_gfx_flush_cache,
};

/**
 * @brief  Create a new graphics surface object
 */
gfx_surface* gfx_create_surface(void* ptr, uint width, uint height, uint stride, gfx_format format,
                                uint32_t flags) {
  return gfx_create_surface_with_context(ptr, &g_kernel_ctx, width, height, stride, format, flags);
}

/**
 * @brief  Create a new graphics surface object from a display
 */
gfx_surface* gfx_create_surface_from_display(struct display_info* info) {
  gfx_surface* surface = static_cast<gfx_surface*>(calloc(1, sizeof(*surface)));
  if (surface == NULL)
    return NULL;
  if (gfx_init_surface_from_display(surface, info)) {
    free(surface);
    return NULL;
  }
  return surface;
}

zx_status_t gfx_init_surface_from_display(gfx_surface* surface, struct display_info* info) {
  zx_status_t r;
  switch (info->format) {
    case ZX_PIXEL_FORMAT_RGB_565:
    case ZX_PIXEL_FORMAT_RGB_332:
    case ZX_PIXEL_FORMAT_RGB_2220:
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_MONO_8:
      // supported formats
      break;
    default:
      dprintf(CRITICAL, "invalid graphics format %x", info->format);
      return ZX_ERR_INVALID_ARGS;
  }

  uint32_t flags = (info->flags & DISPLAY_FLAG_NEEDS_CACHE_FLUSH) ? GFX_FLAG_FLUSH_CPU_CACHE : 0;
  r = gfx_init_surface(surface, info->framebuffer, info->width, info->height, info->stride,
                       info->format, flags);

  surface->flush = info->flush;
  return r;
}

/**
 * @brief  Write a test pattern to the default display.
 */
void gfx_draw_pattern(void) {
  struct display_info info;
  if (display_get_info(&info) < 0)
    return;

  gfx_surface* surface = gfx_create_surface_from_display(&info);
  DEBUG_ASSERT(surface != nullptr);

  uint x, y;
  for (y = 0; y < surface->height; y++) {
    for (x = 0; x < surface->width; x++) {
      uint scaledx;
      uint scaledy;

      scaledx = x * 256 / surface->width;
      scaledy = y * 256 / surface->height;

      gfx_putpixel(surface, x, y,
                   (0xff << 24) | (scaledx * scaledy) << 16 | (scaledx >> 1) << 8 | scaledy >> 1);
    }
  }

  gfx_flush(surface);

  gfx_surface_destroy(surface);
}

/**
 * @brief  Fill default display with white
 */
[[maybe_unused]] static void gfx_draw_pattern_white(void) {
  struct display_info info;
  if (display_get_info(&info) < 0)
    return;

  gfx_surface* surface = gfx_create_surface_from_display(&info);
  DEBUG_ASSERT(surface != nullptr);

  uint x, y;
  for (y = 0; y < surface->height; y++) {
    for (x = 0; x < surface->width; x++) {
      gfx_putpixel(surface, x, y, 0xFFFFFFFF);
    }
  }

  gfx_flush(surface);

  gfx_surface_destroy(surface);
}

#if LK_DEBUGLEVEL > 1
#include <lib/console.h>

static int cmd_gfx(int argc, const cmd_args* argv, uint32_t flags);

STATIC_COMMAND_START
STATIC_COMMAND("gfx", "gfx commands", &cmd_gfx)
STATIC_COMMAND_END(gfx)

static int gfx_draw_rgb_bars(gfx_surface* surface) {
  uint x, y;

  uint step = surface->height * 100 / 256;
  uint color;

  for (y = 0; y < surface->height; y++) {
    // R
    for (x = 0; x < surface->width / 3; x++) {
      color = y * 100 / step;
      gfx_putpixel(surface, x, y, 0xff << 24 | color << 16);
    }
    // G
    for (; x < 2 * (surface->width / 3); x++) {
      color = y * 100 / step;
      gfx_putpixel(surface, x, y, 0xff << 24 | color << 8);
    }
    // B
    for (; x < surface->width; x++) {
      color = y * 100 / step;
      gfx_putpixel(surface, x, y, 0xff << 24 | color);
    }
  }

  return 0;
}

static int cmd_gfx(int argc, const cmd_args* argv, uint32_t flags) {
  if (argc < 2) {
    printf("not enough arguments:\n");
    printf("%s display_info : output information bout the current display\n", argv[0].str);
    printf("%s rgb_bars   : Fill frame buffer with rgb bars\n", argv[0].str);
    printf("%s test_pattern : Fill frame with test pattern\n", argv[0].str);
    printf("%s fill r g b   : Fill frame buffer with RGB888 value and force update\n", argv[0].str);

    return -1;
  }

  struct display_info info;
  if (display_get_info(&info) < 0) {
    printf("no display to draw on!\n");
    return -1;
  }

  gfx_surface* surface = gfx_create_surface_from_display(&info);
  DEBUG_ASSERT(surface != nullptr);

  if (!strcmp(argv[1].str, "display_info")) {
    printf("display:\n");
    printf("\tframebuffer %p\n", info.framebuffer);
    printf("\twidth %u height %u stride %u\n", info.width, info.height, info.stride);
    printf("\tformat 0x%x\n", info.format);
    printf("\tflags 0x%x\n", info.flags);
  } else if (!strcmp(argv[1].str, "rgb_bars")) {
    gfx_draw_rgb_bars(surface);
  } else if (!strcmp(argv[1].str, "test_pattern")) {
    gfx_draw_pattern();
  } else if (!strcmp(argv[1].str, "fill")) {
    uint x, y;

    uint fillval =
        static_cast<uint>((0xff << 24) | (argv[2].u << 16) | (argv[3].u << 8) | argv[4].u);
    for (y = 0; y < surface->height; y++) {
      for (x = 0; x < surface->width; x++) {
        /* write pixel to frame buffer */
        gfx_putpixel(surface, x, y, fillval);
      }
    }
  }

  gfx_flush(surface);

  gfx_surface_destroy(surface);

  return 0;
}

#endif
