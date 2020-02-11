// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_TILING_H_
#define ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_TILING_H_

#include <assert.h>
#include <inttypes.h>
#include <zircon/pixelformat.h>

#include <ddk/protocol/display/controller.h>
#include <ddk/protocol/intelgpucore.h>
#include <fbl/algorithm.h>

namespace i915 {

static inline uint32_t get_tile_byte_width(uint32_t tiling, zx_pixel_format_t format) {
  switch (tiling) {
    case IMAGE_TYPE_SIMPLE:
      return 64;
    case IMAGE_TYPE_X_TILED:
      return 512;
    case IMAGE_TYPE_Y_LEGACY_TILED:
      return 128;
    case IMAGE_TYPE_YF_TILED:
      return ZX_PIXEL_FORMAT_BYTES(format) == 1 ? 64 : 128;
    default:
      assert(false);
      return 0;
  }
}

static inline uint32_t get_tile_byte_size(uint32_t tiling) {
  return tiling == IMAGE_TYPE_SIMPLE ? 64 : 4096;
}

static inline uint32_t get_tile_px_height(uint32_t tiling, zx_pixel_format_t format) {
  return get_tile_byte_size(tiling) / get_tile_byte_width(tiling, format);
}

static inline uint32_t width_in_tiles(uint32_t tiling, uint32_t width, zx_pixel_format_t format) {
  uint32_t tile_width = get_tile_byte_width(tiling, format);
  return ((width * ZX_PIXEL_FORMAT_BYTES(format)) + tile_width - 1) / tile_width;
}

static inline uint32_t height_in_tiles(uint32_t tiling, uint32_t height, zx_pixel_format_t format) {
  uint32_t tile_height = get_tile_px_height(tiling, format);
  return (height + tile_height - 1) / tile_height;
}

}  // namespace i915

#endif  // ZIRCON_SYSTEM_DEV_DISPLAY_INTEL_I915_TILING_H_
