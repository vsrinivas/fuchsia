// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/machina/gpu_bitmap.h"

#include <string.h>

#include "garnet/lib/machina/gpu.h"

GpuBitmap::GpuBitmap() : GpuBitmap(0, 0, nullptr) {}

GpuBitmap::GpuBitmap(GpuBitmap&& o)
    : width_(o.width_),
      height_(o.height_),
      buffer_(fbl::move(o.buffer_)),
      ptr_(o.ptr_) {
  o.ptr_ = nullptr;
}

GpuBitmap::GpuBitmap(uint32_t width, uint32_t height, uint8_t* ptr)
    : width_(width), height_(height), ptr_(ptr) {}

GpuBitmap::GpuBitmap(uint32_t width,
                     uint32_t height)
    : width_(width),
      height_(height),
      buffer_(new uint8_t[width * height * VirtioGpu::kBytesPerPixel]),
      ptr_(buffer_.get()) {}

void GpuBitmap::DrawBitmap(const GpuBitmap& source_bitmap,
                           const GpuRect& source_rect,
                           const GpuRect& dest_rect) {
  if (source_rect.width != dest_rect.width ||
      source_rect.height != dest_rect.height) {
    return;
  }
  if (source_rect.x + source_rect.width > source_bitmap.width() ||
      source_rect.y + source_rect.height > source_bitmap.height()) {
    return;
  }
  if (dest_rect.x + dest_rect.width > width() ||
      dest_rect.y + dest_rect.height > height()) {
    return;
  }

  uint32_t stride = source_rect.width * VirtioGpu::kBytesPerPixel;
  size_t source_offset =
      (source_bitmap.width() * source_rect.y + source_rect.x) *
      VirtioGpu::kBytesPerPixel;
  size_t dest_offset =
      (width() * dest_rect.y + dest_rect.x) * VirtioGpu::kBytesPerPixel;
  uint8_t* source_buf = source_bitmap.buffer();
  uint8_t* dest_buf = buffer();

  // Optimize for the case where we can do a single contiguous copy from source
  // to dest.
  if (dest_rect.width == width() && source_bitmap.width() == width()) {
    memcpy(dest_buf + dest_offset, source_buf + source_offset,
           stride * dest_rect.height);
    return;
  }

  for (uint32_t row = 0; row < source_rect.height; ++row) {
    memcpy(dest_buf + dest_offset, source_buf + source_offset, stride);
    dest_offset += (width() * VirtioGpu::kBytesPerPixel);
    source_offset += (source_bitmap.width() * VirtioGpu::kBytesPerPixel);
  }
}
