// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#ifndef GARNET_LIB_MACHINA_GPU_BITMAP_H_
#define GARNET_LIB_MACHINA_GPU_BITMAP_H_

#include <stdint.h>

#include <fbl/macros.h>
#include <fbl/unique_ptr.h>
#include <zircon/pixelformat.h>
#include <zircon/types.h>

namespace machina {

struct GpuRect {
  uint32_t x;
  uint32_t y;
  uint32_t width;
  uint32_t height;

  // Returns |true| iff any part of |o| overlaps the same area as this rect.
  bool Overlaps(const GpuRect& o);
};

// A contiguous 2D display buffer.
class GpuBitmap {
 public:
  // Create an empty bitmap.
  GpuBitmap();

  // Create a bitmap with an existing buffer.
  GpuBitmap(uint32_t width, uint32_t height, zx_pixel_format_t format,
            uint8_t* buffer);

  // Create a bitmap with an existing buffer.
  GpuBitmap(uint32_t width, uint32_t height, uint32_t stride,
            zx_pixel_format_t format, uint8_t* buffer);

  // Create a bitmap for a given size.
  GpuBitmap(uint32_t width, uint32_t height, zx_pixel_format_t format);

  // Move semantics.
  GpuBitmap(GpuBitmap&&);
  GpuBitmap& operator=(GpuBitmap&& o);

  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(GpuBitmap);

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  uint32_t stride() const { return stride_; }
  zx_pixel_format_t format() const { return format_; }
  uint8_t pixelsize() const { return ZX_PIXEL_FORMAT_BYTES(format_); }
  uint8_t* buffer() const { return ptr_; }

  enum class DrawBitmapFlags : uint32_t {
    NONE = 0,
    FORCE_ALPHA = 1, // force interpretation of the x-channel as alpha
  };

  // Draws a portion of another bitmap into this one.
  //
  // |source_rect| and |dest_rect| must both be wholly contained within
  // the respective bitmaps and must have the same width and height.
  void DrawBitmap(const GpuBitmap& from, const GpuRect& source_rect,
                  const GpuRect& dest_rect,
                  DrawBitmapFlags flags = DrawBitmapFlags::NONE);

 private:
  uint32_t width_;
  uint32_t height_;
  uint32_t stride_;
  zx_pixel_format_t format_;

  // Reading of the buffer should always occur through |ptr_| as |buffer_| is
  // not used when operating when an externally-managed buffer.
  fbl::unique_ptr<uint8_t[]> buffer_;
  uint8_t* ptr_;
};

inline GpuBitmap::DrawBitmapFlags operator&(GpuBitmap::DrawBitmapFlags a,
                                            GpuBitmap::DrawBitmapFlags b) {
  return static_cast<GpuBitmap::DrawBitmapFlags>(static_cast<uint32_t>(a) &
                                                 static_cast<uint32_t>(b));
}
inline GpuBitmap::DrawBitmapFlags operator|(GpuBitmap::DrawBitmapFlags a,
                                            GpuBitmap::DrawBitmapFlags b) {
  return static_cast<GpuBitmap::DrawBitmapFlags>(static_cast<uint32_t>(a) |
                                                 static_cast<uint32_t>(b));
}

}  // namespace machina

#endif  // GARNET_LIB_MACHINA_GPU_BITMAP_H_
