// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "color_source.h"

#include <zircon/assert.h>
#include <zircon/syscalls.h>

#include <src/lib/fxl/arraysize.h>
#include <src/lib/fxl/log_level.h>
#include <src/lib/fxl/logging.h>

namespace virtual_camera {

void ColorSource::FillARGB(void* start, size_t buffer_size) {
  if (!start) {
    FXL_LOG(ERROR) << "Must pass a valid buffer pointer";
    return;
  }
  uint8_t r, g, b;
  hsv_color(frame_color_, &r, &g, &b);
  FXL_VLOG(4) << "Filling with " << (int)r << " " << (int)g << " " << (int)b;
  uint32_t color = 0xff << 24 | r << 16 | g << 8 | b;
  ZX_DEBUG_ASSERT(buffer_size % 4 == 0);
  uint32_t num_pixels = buffer_size / 4;
  uint32_t* pixels = reinterpret_cast<uint32_t*>(start);
  for (unsigned int i = 0; i < num_pixels; i++) {
    pixels[i] = color;
  }

  // Ignore if flushing the cache fails.
  zx_cache_flush(start, buffer_size, ZX_CACHE_FLUSH_DATA | ZX_CACHE_FLUSH_INVALIDATE);
  frame_color_ += kFrameColorInc;
  if (frame_color_ > kMaxFrameColor) {
    frame_color_ -= kMaxFrameColor;
  }
}

void ColorSource::hsv_color(uint32_t index, uint8_t* r, uint8_t* g, uint8_t* b) {
  uint8_t pos = index & 0xff;
  uint8_t neg = 0xff - (index & 0xff);
  uint8_t phase = (index >> 8) & 0x7;
  uint8_t phases[6] = {0xff, 0xff, neg, 0x00, 0x00, pos};
  *r = phases[(phase + 1) % arraysize(phases)];
  *g = phases[(phase + 5) % arraysize(phases)];
  *b = phases[(phase + 3) % arraysize(phases)];
}

}  // namespace virtual_camera
