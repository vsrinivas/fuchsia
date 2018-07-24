// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_bitmap.h"

#include <string.h>

#include "garnet/lib/machina/virtio_gpu.h"

namespace machina {

static constexpr uint8_t kSrcPixelSize = 4;

bool GpuRect::Overlaps(const GpuRect& o) {
  if (x > (o.x + o.width) || o.x > (x + width)) {
    return false;
  }
  if (y > (o.y + o.height) || o.y > (y + height)) {
    return false;
  }
  return true;
}

// NOTE(abdulla): These functions are lightly modified versions of the same
// functions in the Zircon GFX library.

static void argb8888_to_rgb565(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 2, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint16_t out = (in >> 3) & 0x1f;   // b
    out |= ((in >> 10) & 0x3f) << 5;   // g
    out |= ((in >> 19) & 0x1f) << 11;  // r
    *reinterpret_cast<uint16_t*>(dst) = out;
  }
}

static void argb8888_to_rgb332(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint8_t out = (in >> 6) & 0x3;   // b
    out |= ((in >> 13) & 0x7) << 2;  // g
    out |= ((in >> 21) & 0x7) << 5;  // r
    *dst = out;
  }
}

static void argb8888_to_rgb2220(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint8_t out = ((in >> 6) & 0x3) << 2;  // b
    out |= ((in >> 14) & 0x3) << 4;        // g
    out |= ((in >> 22) & 0x3) << 6;        // r
    *dst = out;
  }
}

static void argb8888_to_luma(uint8_t* dst, const uint8_t* src, size_t size) {
  for (size_t i = 0; i < size; i += 4, dst += 1, src += 4) {
    uint32_t in = *reinterpret_cast<const uint32_t*>(src);
    uint32_t b = (in & 0xff) * 74;
    uint32_t g = ((in >> 8) & 0xff) * 732;
    uint32_t r = ((in >> 16) & 0xff) * 218;
    uint32_t intensity = r + b + g;
    *dst = (intensity >> 10) & 0xff;
  }
}

static void copy(uint8_t* dst, const uint8_t* src, size_t size,
                 zx_pixel_format_t format) {
  switch (format) {
    case ZX_PIXEL_FORMAT_ARGB_8888:
    case ZX_PIXEL_FORMAT_RGB_x888:
      return static_cast<void>(memcpy(dst, src, size));
    case ZX_PIXEL_FORMAT_RGB_565:
      return argb8888_to_rgb565(dst, src, size);
    case ZX_PIXEL_FORMAT_RGB_332:
      return argb8888_to_rgb332(dst, src, size);
    case ZX_PIXEL_FORMAT_RGB_2220:
      return argb8888_to_rgb2220(dst, src, size);
    case ZX_PIXEL_FORMAT_GRAY_8:
      return argb8888_to_luma(dst, src, size);
    default:
      ZX_DEBUG_ASSERT(false);
  }
}

static void blend(uint8_t* dst, const uint8_t* src,
                  zx_pixel_format_t dst_format, zx_pixel_format_t src_format) {
  // ARGB is currently the only ZX_PIXEL_FORMAT with an alpha channel.
  ZX_DEBUG_ASSERT(src_format == ZX_PIXEL_FORMAT_ARGB_8888);
  uint32_t src1 = *reinterpret_cast<const uint32_t*>(src);
  float src1b = (src1 & 0xff) / 255.0f;
  float src1g = ((src1 >> 8) & 0xff) / 255.0f;
  float src1r = ((src1 >> 16) & 0xff) / 255.0f;
  float src1a = ((src1 >> 24) & 0xff) / 255.0f;
  uint32_t src2 = *reinterpret_cast<const uint32_t*>(dst);
  float src2b = (src2 & 0xff) / 255.0f;
  float src2g = ((src2 >> 8) & 0xff) / 255.0f;
  float src2r = ((src2 >> 16) & 0xff) / 255.0f;
  float src2a = ((src2 >> 24) & 0xff) / 255.0f;
  float dstb = src1b * src1a + src2b * (1.0f - src1a);
  float dstg = src1g * src1a + src2g * (1.0f - src1a);
  float dstr = src1r * src1a + src2r * (1.0f - src1a);
  float dsta = 1.0f - (1.0f - src1a) * (1.0f - src2a);
  uint32_t dst_rgba = static_cast<uint32_t>(dstb * 255.0f + 0.5f) |
                      (static_cast<uint32_t>(dstg * 255.0f + 0.5f) << 8) |
                      (static_cast<uint32_t>(dstr * 255.0f + 0.5f) << 16) |
                      (static_cast<uint32_t>(dsta * 255.0f + 0.5f) << 24);
  copy(dst, reinterpret_cast<const uint8_t*>(&dst_rgba), kSrcPixelSize,
       dst_format);
}

GpuBitmap::GpuBitmap() : GpuBitmap(0, 0, 0, nullptr) {}

GpuBitmap::GpuBitmap(uint32_t width, uint32_t height, zx_pixel_format_t format,
                     uint8_t* ptr)
    : GpuBitmap(width, height, width, format, ptr) {}

GpuBitmap::GpuBitmap(uint32_t width, uint32_t height, uint32_t stride,
                     zx_pixel_format_t format, uint8_t* ptr)
    : width_(width),
      height_(height),
      stride_(stride),
      format_(format),
      ptr_(ptr) {}

GpuBitmap::GpuBitmap(uint32_t width, uint32_t height, zx_pixel_format_t format)
    : width_(width),
      height_(height),
      stride_(width),
      format_(format),
      buffer_(new uint8_t[width * height * pixelsize()]),
      ptr_(buffer_.get()) {}

GpuBitmap::GpuBitmap(GpuBitmap&& o)
    : width_(o.width_),
      height_(o.height_),
      stride_(o.stride_),
      format_(o.format_),
      buffer_(std::move(o.buffer_)),
      ptr_(o.ptr_) {
  o.ptr_ = nullptr;
}

GpuBitmap& GpuBitmap::operator=(GpuBitmap&& o) {
  width_ = o.width_;
  height_ = o.height_;
  stride_ = o.stride_;
  format_ = o.format_;
  buffer_ = std::move(o.buffer_);
  ptr_ = o.ptr_;
  o.ptr_ = nullptr;
  return *this;
}

void GpuBitmap::DrawBitmap(const GpuBitmap& src_bitmap, const GpuRect& src_rect,
                           const GpuRect& dst_rect, DrawBitmapFlags flags) {
  if (src_rect.width != dst_rect.width || src_rect.height != dst_rect.height) {
    return;
  }
  if (src_bitmap.pixelsize() != kSrcPixelSize) {
    return;
  }
  if (src_rect.x > src_bitmap.width() || src_rect.y > src_bitmap.height() ||
      dst_rect.x > width() || dst_rect.y > height()) {
    return;
  }
  // TODO: Turn these into clamps.
  if (src_rect.x + src_rect.width > src_bitmap.width() ||
      src_rect.y + src_rect.height > src_bitmap.height()) {
    return;
  }
  if (dst_rect.x + dst_rect.width > width() ||
      dst_rect.y + dst_rect.height > height()) {
    return;
  }

  size_t copy_stride = src_rect.width * src_bitmap.pixelsize();
  size_t src_offset =
      (src_bitmap.stride() * src_rect.y + src_rect.x) * src_bitmap.pixelsize();
  size_t dst_offset = (stride() * dst_rect.y + dst_rect.x) * pixelsize();
  uint8_t* src_buf = src_bitmap.buffer();
  uint8_t* dst_buf = buffer();

  // Check whether to interpret the source as an alpha-enabled format variant.
  zx_pixel_format_t src_format_interpretation = ZX_PIXEL_FORMAT_NONE;
  if ((flags & DrawBitmapFlags::FORCE_ALPHA) != DrawBitmapFlags::NONE) {
    switch (src_bitmap.format()) {
      case ZX_PIXEL_FORMAT_RGB_x888:
        src_format_interpretation = ZX_PIXEL_FORMAT_ARGB_8888;
        break;
      default:
        src_format_interpretation = src_bitmap.format();
        break;
    }
  }

  // Alpha blend alpha-enabled surfaces (currently only ARGB).
  // This must be done per-pixel.
  if (src_format_interpretation == ZX_PIXEL_FORMAT_ARGB_8888) {
    for (uint32_t row = 0; row < src_rect.height; ++row) {
      for (uint32_t col = 0; col < src_rect.width; ++col) {
        blend(dst_buf + dst_offset + col * pixelsize(),
              src_buf + src_offset + col * src_bitmap.pixelsize(), format(),
              src_format_interpretation);
      }
      src_offset += src_bitmap.stride() * src_bitmap.pixelsize();
      dst_offset += stride() * pixelsize();
    }
  } else {
    // Opaque bitmaps can be blitted directly (i.e. copy).
    // Optimize for the case where we can do a single copy from src to dst.
    if (dst_rect.width == width() && src_bitmap.stride() == stride()) {
      copy(dst_buf + dst_offset, src_buf + src_offset,
           copy_stride * dst_rect.height, format());
      return;
    }

    // Otherwise, copy each row (guaranteed contiguous).
    for (uint32_t row = 0; row < src_rect.height; ++row) {
      copy(dst_buf + dst_offset, src_buf + src_offset, copy_stride, format());
      src_offset += src_bitmap.stride() * src_bitmap.pixelsize();
      dst_offset += stride() * pixelsize();
    }
  }
}

}  // namespace machina
