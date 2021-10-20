// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/**
 * @defgroup graphics Graphics
 *
 * @{
 */

/**
 * @file
 * @brief  Graphics drawing library
 */
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <zircon/compiler.h>
#include <zircon/syscalls.h>

#include <gfx-common/gfx-common.h>

#define GFX_LOG(ctx, fmt, ...)                                     \
  do {                                                             \
    (ctx)->log("[%s:%d] " fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
  } while (0)

#define GFX_ASSERT_MSG(ctx, cond, fmt, ...)                           \
  do {                                                                \
    if (!(cond)) {                                                    \
      (ctx)->panic("[%s:%d]" fmt, __FILE__, __LINE__, ##__VA_ARGS__); \
    }                                                                 \
  } while (0)

#define GFX_ASSERT(ctx, cond)                                                    \
  do {                                                                           \
    if (!(cond)) {                                                               \
      (ctx)->panic("[%s:%d] failed assertion: " #cond "\n", __FILE__, __LINE__); \
    }                                                                            \
  } while (0)

namespace {

struct Rgb888 {
  Rgb888() = default;
  explicit Rgb888(uint32_t rgba) {
    b = rgba & 0xff;
    g = (rgba & 0xff00) >> 8;
    r = (rgba & 0xff0000) >> 16;
  }
  uint32_t ToRgba32() const { return b | (g << 8) | (r << 16) | (0xff << 24); }

  uint8_t b = 0;
  uint8_t g = 0;
  uint8_t r = 0;
} __attribute__((packed));

}  // namespace

// Convert a 32bit ARGB image to its respective gamma corrected grayscale value.
static uint32_t ARGB8888_to_Luma(uint32_t in) {
  uint8_t out;

  uint32_t blue = (in & 0xFF) * 74;
  uint32_t green = ((in >> 8) & 0xFF) * 732;
  uint32_t red = ((in >> 16) & 0xFF) * 218;

  uint32_t intensity = red + blue + green;

  out = (intensity >> 10) & 0xFF;

  return out;
}

static uint32_t ARGB8888_to_RGB888(uint32_t in) { return in & 0xFFFFFF; }

static uint32_t ARGB8888_to_RGB565(uint32_t in) {
  uint32_t out;

  out = (in >> 3) & 0x1f;            // b
  out |= ((in >> 10) & 0x3f) << 5;   // g
  out |= ((in >> 19) & 0x1f) << 11;  // r

  return out;
}

static uint32_t ARGB8888_to_RGB332(uint32_t in) {
  uint32_t out = 0;

  out = (in >> 6) & 0x3;           // b
  out |= ((in >> 13) & 0x7) << 2;  // g
  out |= ((in >> 21) & 0x7) << 5;  // r

  return out;
}

static uint32_t ARGB8888_to_RGB2220(uint32_t in) {
  uint32_t out = 0;

  out = ((in >> 6) & 0x3) << 2;
  out |= ((in >> 14) & 0x3) << 4;
  out |= ((in >> 22) & 0x3) << 6;

  return out;
}

/**
 * @brief  Copy a rectangle of pixels from one part of the display to another.
 */
void gfx_copyrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t x2, uint32_t y2) {
  // trim
  if (x >= surface->width)
    return;
  if (x2 >= surface->width)
    return;
  if (y >= surface->height)
    return;
  if (y2 >= surface->height)
    return;
  if (width == 0 || height == 0)
    return;

  // clip the width to src or dest
  if (x + width > surface->width)
    width = surface->width - x;
  if (x2 + width > surface->width)
    width = surface->width - x2;

  // clip the height to src or dest
  if (y + height > surface->height)
    height = surface->height - y;
  if (y2 + height > surface->height)
    height = surface->height - y2;

  surface->copyrect(surface, x, y, width, height, x2, y2);
}

/**
 * @brief  Fill a rectangle on the screen with a constant color.
 */
void gfx_fillrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                  uint32_t color) {
  GFX_LOG(surface->ctx, "surface %p, x %u y %u w %u h %u c %u\n", surface, x, y, width, height,
          color);
  // trim
  if (unlikely(x >= surface->width))
    return;
  if (y >= surface->height)
    return;
  if (width == 0 || height == 0)
    return;

  // clip the width
  if (x + width > surface->width)
    width = surface->width - x;

  // clip the height
  if (y + height > surface->height)
    height = surface->height - y;

  surface->fillrect(surface, x, y, width, height, color);
}

/**
 * @brief  Write a single pixel to the screen.
 */
void gfx_putpixel(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t color) {
  if (unlikely(x >= surface->width))
    return;
  if (y >= surface->height)
    return;

  surface->putpixel(surface, x, y, color);
}

template <typename T>
static void putpixel(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t color) {
  T* dest = static_cast<T*>(surface->ptr) + (x + y * surface->stride);

  if (sizeof(T) == sizeof(uint32_t)) {
    *dest = static_cast<T>(color);
  } else {
    // colors come in in ARGB 8888 form, flatten them
    *dest = static_cast<T>(surface->translate_color(color));
  }
}

template <typename T>
static void copyrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                     uint32_t x2, uint32_t y2) {
  // copy
  const T* src = static_cast<const T*>(surface->ptr) + (x + y * surface->stride);
  T* dest = static_cast<T*>(surface->ptr) + (x2 + y2 * surface->stride);
  uint32_t stride_diff = surface->stride - width;

  if (dest < src) {
    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = *src;
        dest++;
        src++;
      }
      dest += stride_diff;
      src += stride_diff;
    }
  } else {
    // copy backwards
    src += (height - 1) * surface->stride + (width - 1);
    dest += (height - 1) * surface->stride + (width - 1);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = *src;
        dest--;
        src--;
      }
      dest -= stride_diff;
      src -= stride_diff;
    }
  }
}

template <typename T>
static void fillrect(gfx_surface* surface, uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                     uint32_t _color) {
  T* dest = static_cast<T*>(surface->ptr) + (x + y * surface->stride);
  uint32_t stride_diff = surface->stride - width;

  T color;
  if (sizeof(_color) == sizeof(color)) {
    color = static_cast<T>(_color);
  } else {
    color = static_cast<T>(surface->translate_color(_color));
  }

  uint32_t i, j;
  for (i = 0; i < height; i++) {
    for (j = 0; j < width; j++) {
      *dest = color;
      dest++;
    }
    dest += stride_diff;
  }
}

void gfx_line(gfx_surface* surface, uint32_t x1, uint32_t y1, uint32_t x2, uint32_t y2,
              uint32_t color) {
  if (unlikely(x1 >= surface->width))
    return;
  if (unlikely(x2 >= surface->width))
    return;

  if (y1 >= surface->height)
    return;
  if (y2 >= surface->height)
    return;

  int dx = x2 - x1;
  int dy = y2 - y1;

  int sdx = (0 < dx) - (dx < 0);
  int sdy = (0 < dy) - (dy < 0);

  uint32_t dxabs = (dx > 0) ? dx : -dx;
  uint32_t dyabs = (dy > 0) ? dy : -dy;

  uint32_t x = dyabs >> 1;
  uint32_t y = dxabs >> 1;

  uint32_t px = x1;
  uint32_t py = y1;

  if (dxabs >= dyabs) {
    // mostly horizontal line.
    for (uint32_t i = 0; i < dxabs; i++) {
      y += dyabs;
      if (y >= dxabs) {
        y -= dxabs;
        py += sdy;
      }
      px += sdx;
      surface->putpixel(surface, px, py, color);
    }
  } else {
    // mostly vertical line.
    for (uint32_t i = 0; i < dyabs; i++) {
      x += dxabs;
      if (x >= dyabs) {
        x -= dyabs;
        px += sdx;
      }
      py += sdy;
      surface->putpixel(surface, px, py, color);
    }
  }
}

static uint32_t alpha32_add_ignore_destalpha(uint32_t dest, uint32_t src) {
  uint32_t cdest[3];
  uint32_t csrc[3];

  uint32_t srca;
  uint32_t srcainv;

  srca = (src >> 24) & 0xff;
  if (srca == 0) {
    return dest;
  } else if (srca == 255) {
    return src;
  }
  srca++;
  srcainv = (255 - srca);

  cdest[0] = (dest >> 16) & 0xff;
  cdest[1] = (dest >> 8) & 0xff;
  cdest[2] = (dest >> 0) & 0xff;

  csrc[0] = (src >> 16) & 0xff;
  csrc[1] = (src >> 8) & 0xff;
  csrc[2] = (src >> 0) & 0xff;

  //    if (srca > 0)
  //        printf("s %d %d %d d %d %d %d a %d ai %d\n", csrc[0], csrc[1], csrc[2], cdest[0],
  //        cdest[1], cdest[2], srca, srcainv);

  uint32_t cres[3];

  cres[0] = ((csrc[0] * srca) / 256) + ((cdest[0] * srcainv) / 256);
  cres[1] = ((csrc[1] * srca) / 256) + ((cdest[1] * srcainv) / 256);
  cres[2] = ((csrc[2] * srca) / 256) + ((cdest[2] * srcainv) / 256);

  return (srca << 24) | (cres[0] << 16) | (cres[1] << 8) | (cres[2]);
}

/**
 * @brief  Copy pixels from source to dest.
 *
 * Currently does not support alpha channel.
 */
void gfx_surface_blend(struct gfx_surface* target, struct gfx_surface* source, uint32_t destx,
                       uint32_t desty) {
  gfx_blend(target, source, 0, 0, source->width, source->height, destx, desty);
}

void gfx_blend(struct gfx_surface* target, struct gfx_surface* source, uint32_t srcx, uint32_t srcy,
               uint32_t width, uint32_t height, uint32_t destx, uint32_t desty) {
  const gfx_context* ctx = source->ctx;
  GFX_ASSERT(ctx, target->format == source->format);
  GFX_LOG(ctx, "target %p, source %p, destx %u, desty %u\n", target, source, destx, desty);

  if (destx >= target->width)
    return;
  if (desty >= target->height)
    return;

  if (srcx >= source->width)
    return;
  if (srcy >= source->height)
    return;

  if (destx + width > target->width)
    width = target->width - destx;
  if (desty + height > target->height)
    height = target->height - desty;

  if (srcx + width > source->width)
    width = source->width - srcx;
  if (srcy + height > source->height)
    height = source->height - srcy;

  // XXX total hack to deal with various blends
  if (source->format == ZX_PIXEL_FORMAT_RGB_565 && target->format == ZX_PIXEL_FORMAT_RGB_565) {
    // 16 bit to 16 bit
    const uint16_t* src = static_cast<uint16_t*>(source->ptr) + (srcx + srcy * source->stride);
    uint16_t* dest = static_cast<uint16_t*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = *src;
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else if (source->format == ZX_PIXEL_FORMAT_ARGB_8888 &&
             target->format == ZX_PIXEL_FORMAT_ARGB_8888) {
    // both are 32 bit modes, both alpha
    const uint32_t* src = static_cast<uint32_t*>(source->ptr) + (srcx + srcy * source->stride);
    uint32_t* dest = static_cast<uint32_t*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        // TODO(fxbug.dev/84457): Currently it ignores destination alpha.
        // We should implement alpha blending correctly.
        *dest = alpha32_add_ignore_destalpha(*dest, *src);
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else if (source->format == ZX_PIXEL_FORMAT_RGB_x888 &&
             target->format == ZX_PIXEL_FORMAT_RGB_x888) {
    // both are 32 bit modes, no alpha
    const uint32_t* src = static_cast<uint32_t*>(source->ptr) + (srcx + srcy * source->stride);
    uint32_t* dest = static_cast<uint32_t*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = *src;
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else if (source->format == ZX_PIXEL_FORMAT_ARGB_8888 &&
             target->format == ZX_PIXEL_FORMAT_RGB_888) {
    // 32 bit to 24 bit modes, alpha to no-alpha
    const uint32_t* src = static_cast<uint32_t*>(source->ptr) + (srcx + srcy * source->stride);
    Rgb888* dest = static_cast<Rgb888*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        // TODO(fxbug.dev/84457): Currently it ignores destination alpha.
        // We should implement alpha blending correctly.
        *dest = Rgb888(alpha32_add_ignore_destalpha(dest->ToRgba32(), *src));
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else if (source->format == ZX_PIXEL_FORMAT_RGB_x888 &&
             target->format == ZX_PIXEL_FORMAT_RGB_888) {
    // 32 bit to 24 bit modes, no alpha
    const uint32_t* src = static_cast<uint32_t*>(source->ptr) + (srcx + srcy * source->stride);
    Rgb888* dest = static_cast<Rgb888*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = Rgb888(*src);
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else if (source->format == ZX_PIXEL_FORMAT_MONO_8 && target->format == ZX_PIXEL_FORMAT_MONO_8) {
    // both are 8 bit modes, no alpha
    const uint8_t* src = static_cast<uint8_t*>(source->ptr) + (srcx + srcy * source->stride);
    uint8_t* dest = static_cast<uint8_t*>(target->ptr) + (destx + desty * target->stride);
    uint32_t dest_stride_diff = target->stride - width;
    uint32_t source_stride_diff = source->stride - width;

    GFX_LOG(ctx, "w %u h %u dstride %u sstride %u\n", width, height, dest_stride_diff,
            source_stride_diff);

    uint32_t i, j;
    for (i = 0; i < height; i++) {
      for (j = 0; j < width; j++) {
        *dest = *src;
        dest++;
        src++;
      }
      dest += dest_stride_diff;
      src += source_stride_diff;
    }
  } else {
    GFX_ASSERT_MSG(
        ctx, false,
        "gfx_surface_blend: unimplemented colorspace combination (source %u target %u)\n",
        source->format, target->format);
  }
}

template <typename T>
static void putchar(gfx_surface* surface, const struct gfx_font* font, uint32_t ch, uint32_t x,
                    uint32_t y, uint32_t fg, uint32_t bg) {
  T* dest = static_cast<T*>(surface->ptr) + (x + y * surface->stride);

  const uint16_t* cdata = font->data + ch * font->height;
  uint32_t fw = font->width;
  for (uint32_t i = font->height; i > 0; i--) {
    uint16_t xdata = *cdata++;
    for (uint32_t j = fw; j > 0; j--) {
      *dest++ = static_cast<T>((xdata & 1) ? fg : bg);
      xdata = static_cast<uint16_t>(xdata >> 1);
    }
    dest += (surface->stride - fw);
  }
}

void gfx_putchar(gfx_surface* surface, const struct gfx_font* font, uint32_t ch, uint32_t x,
                 uint32_t y, uint32_t fg, uint32_t bg) {
  if (unlikely(ch > 127)) {
    return;
  }
  if (unlikely(x > (surface->width - font->width))) {
    return;
  }
  if (unlikely(y > (surface->height - font->height))) {
    return;
  }
  if (surface->translate_color) {
    fg = surface->translate_color(fg);
    bg = surface->translate_color(bg);
  }
  surface->putchar(surface, font, ch, x, y, fg, bg);
}

void gfx_copylines(gfx_surface* dst, gfx_surface* src, uint32_t srcy, uint32_t dsty,
                   uint32_t height) {
  if ((dst->stride != src->stride) || (dst->format != src->format)) {
    return;
  }
  if ((srcy >= src->height) || ((src->height - srcy) < height)) {
    return;
  }
  if ((dsty >= dst->height) || (dst->height - dsty) < height) {
    return;
  }
  memmove(reinterpret_cast<uint8_t*>(dst->ptr) + dsty * dst->stride * dst->pixelsize,
          reinterpret_cast<uint8_t*>(src->ptr) + srcy * src->stride * src->pixelsize,
          height * src->stride * src->pixelsize);
}

/**
 * @brief  Ensure all graphics rendering is sent to display
 */
void gfx_flush(gfx_surface* surface) {
  if (surface->flags & GFX_FLAG_FLUSH_CPU_CACHE)
    surface->ctx->flush_cache(surface->ptr, surface->len);

  if (surface->flush)
    surface->flush(0, surface->height - 1);
}

/**
 * @brief  Ensure that a sub-region of the display is up to date.
 */
void gfx_flush_rows(struct gfx_surface* surface, uint32_t start, uint32_t end) {
  if (start > end) {
    uint32_t temp = start;
    start = end;
    end = temp;
  }

  if (start >= surface->height)
    return;
  if (end >= surface->height)
    end = surface->height - 1;

  if (surface->flags & GFX_FLAG_FLUSH_CPU_CACHE) {
    uint32_t runlen = surface->stride * surface->pixelsize;
    surface->ctx->flush_cache(reinterpret_cast<uint8_t*>(surface->ptr) + start * runlen,
                              (end - start + 1) * runlen);
  }

  if (surface->flush)
    surface->flush(start, end);
}

/**
 * @brief  Create a new graphics surface object
 */
gfx_surface* gfx_create_surface_with_context(void* ptr, const gfx_context* ctx, uint32_t width,
                                             uint32_t height, uint32_t stride, gfx_format format,
                                             uint32_t flags) {
  gfx_surface* surface = static_cast<gfx_surface*>(calloc(1, sizeof(*surface)));
  if (surface == NULL)
    return NULL;
  surface->ctx = ctx;
  if (gfx_init_surface(surface, ptr, width, height, stride, format, flags)) {
    free(surface);
    return NULL;
  }
  return surface;
}

zx_status_t gfx_init_surface(gfx_surface* surface, void* ptr, uint32_t width, uint32_t height,
                             uint32_t stride, gfx_format format, uint32_t flags) {
  if ((width == 0) || (height == 0) || (stride < width)) {
    return ZX_ERR_INVALID_ARGS;
  }

  surface->flags = flags;
  surface->format = format;
  surface->width = width;
  surface->height = height;
  surface->stride = stride;
  surface->alpha = MAX_ALPHA;

  // set up some function pointers
  switch (format) {
    case ZX_PIXEL_FORMAT_RGB_565:
      surface->translate_color = &ARGB8888_to_RGB565;
      surface->copyrect = &copyrect<uint16_t>;
      surface->fillrect = &fillrect<uint16_t>;
      surface->putpixel = &putpixel<uint16_t>;
      surface->putchar = &putchar<uint16_t>;
      surface->pixelsize = 2;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    case ZX_PIXEL_FORMAT_RGB_888:
      surface->translate_color = &ARGB8888_to_RGB888;
      surface->copyrect = &copyrect<Rgb888>;
      surface->fillrect = &fillrect<Rgb888>;
      surface->putpixel = &putpixel<Rgb888>;
      surface->putchar = &putchar<Rgb888>;
      surface->pixelsize = 3;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    case ZX_PIXEL_FORMAT_RGB_x888:
    case ZX_PIXEL_FORMAT_ARGB_8888:
      surface->translate_color = NULL;
      surface->copyrect = &copyrect<uint32_t>;
      surface->fillrect = &fillrect<uint32_t>;
      surface->putpixel = &putpixel<uint32_t>;
      surface->putchar = &putchar<uint32_t>;
      surface->pixelsize = 4;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    case ZX_PIXEL_FORMAT_MONO_8:
      surface->translate_color = &ARGB8888_to_Luma;
      surface->copyrect = &copyrect<uint8_t>;
      surface->fillrect = &fillrect<uint8_t>;
      surface->putpixel = &putpixel<uint8_t>;
      surface->putchar = &putchar<uint8_t>;
      surface->pixelsize = 1;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    case ZX_PIXEL_FORMAT_RGB_332:
      surface->translate_color = &ARGB8888_to_RGB332;
      surface->copyrect = &copyrect<uint8_t>;
      surface->fillrect = &fillrect<uint8_t>;
      surface->putpixel = &putpixel<uint8_t>;
      surface->putchar = &putchar<uint8_t>;
      surface->pixelsize = 1;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    case ZX_PIXEL_FORMAT_RGB_2220:
      surface->translate_color = &ARGB8888_to_RGB2220;
      surface->copyrect = &copyrect<uint8_t>;
      surface->fillrect = &fillrect<uint8_t>;
      surface->putpixel = &putpixel<uint8_t>;
      surface->putchar = &putchar<uint8_t>;
      surface->pixelsize = 1;
      surface->len = (surface->height * surface->stride * surface->pixelsize);
      break;
    default:
      GFX_LOG(surface->ctx, "invalid graphics format\n");
      return ZX_ERR_INVALID_ARGS;
  }

  if (ptr == NULL) {
    // allocate a buffer
    ptr = malloc(surface->len);
    if (ptr == NULL) {
      return ZX_ERR_NO_MEMORY;
    }
    GFX_ASSERT(surface->ctx, ptr);
    surface->flags |= GFX_FLAG_FREE_ON_DESTROY;
  }
  surface->ptr = ptr;
  return ZX_OK;
}

/**
 * @brief  Destroy a graphics surface and free all resources allocated to it.
 *
 * @param  surface  Surface to destroy.  This pointer is no longer valid after
 *    this call.
 */
void gfx_surface_destroy(struct gfx_surface* surface) {
  if (surface->flags & GFX_FLAG_FREE_ON_DESTROY)
    free(surface->ptr);
  free(surface);
}
