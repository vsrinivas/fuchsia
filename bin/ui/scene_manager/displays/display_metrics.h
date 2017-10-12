// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "lib/fxl/logging.h"

namespace scene_manager {

// Describes the measurements and scale factors used to layout and render
// user interfaces and other graphical content on a particular a display.
class DisplayMetrics {
 public:
  constexpr DisplayMetrics(uint32_t width_in_px,
                           uint32_t height_in_px,
                           float x_scale_in_px_per_gr,
                           float y_scale_in_px_per_gr,
                           float density_in_gr_per_mm)
      : width_in_px_(width_in_px),
        height_in_px_(height_in_px),
        x_scale_in_px_per_gr_(x_scale_in_px_per_gr),
        y_scale_in_px_per_gr_(y_scale_in_px_per_gr),
        density_in_gr_per_mm_(density_in_gr_per_mm) {
    FXL_DCHECK(width_in_px_ > 0u);
    FXL_DCHECK(height_in_px_ > 0u);
    FXL_DCHECK(x_scale_in_px_per_gr_ > 0.f);
    FXL_DCHECK(y_scale_in_px_per_gr_ > 0.f);
    FXL_DCHECK(density_in_gr_per_mm_ >= 0.f);
  }

  // PIXEL METRICS

  // The width of the visible content area in pixels.
  uint32_t width_in_px() const { return width_in_px_; }

  // The height of the visible content area in pixels.
  uint32_t height_in_px() const { return height_in_px_; }

  // PHYSICAL METRICS

  // The physical width of the visible content area in millimeters.
  // Value is 0.0 if unknown.
  float width_in_mm() const { return width_in_gr() * density_in_mm_per_gr(); }

  // The physical height of the visible content area in millimeters.
  // Value is 0.0 if unknown.
  float height_in_mm() const { return height_in_gr() * density_in_mm_per_gr(); }

  // GRID METRICS

  // The width of the visible content area in grids.
  float width_in_gr() const { return width_in_px_ / x_scale_in_px_per_gr_; }

  // The height of the visible content area in grids.
  float height_in_gr() const { return height_in_px_ / y_scale_in_px_per_gr_; }

  // The grid scale factor in pixels per grid in X dimension.
  float x_scale_in_px_per_gr() const { return x_scale_in_px_per_gr_; }

  // The grid scale factor in pixels per grid in Y dimension.
  float y_scale_in_px_per_gr() const { return y_scale_in_px_per_gr_; }

  // The grid scale factor in grids per pixel in X dimension.
  float x_scale_in_gr_per_px() const { return 1.f / x_scale_in_px_per_gr_; }

  // The grid scale factor in grids per pixel in Y dimension.
  float y_scale_in_gr_per_px() const { return 1.f / y_scale_in_px_per_gr_; }

  // The grid density in grids per millimeter.
  // Value is 0.0 if unknown.
  float density_in_gr_per_mm() const { return density_in_gr_per_mm_; }

  // The grid density in millimeters per grid.
  // Value is 0.0 if unknown.
  float density_in_mm_per_gr() const {
    return density_in_gr_per_mm_ != 0.f ? 1.f / density_in_gr_per_mm_ : 0.f;
  }

 private:
  const uint32_t width_in_px_;
  const uint32_t height_in_px_;
  const float x_scale_in_px_per_gr_;
  const float y_scale_in_px_per_gr_;
  const float density_in_gr_per_mm_;
};

}  // namespace scene_manager
