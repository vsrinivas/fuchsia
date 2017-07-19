// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

namespace scene_manager {

// Display is a placeholder that provides make-believe values for screen
// resolution, vsync interval, last vsync time, etc.
class Display {
 public:
  // TODO(MZ-124): We should derive an appropriate value from the rendering
  // targets, in particular giving priority to couple to the display refresh
  // (vsync).
  static constexpr uint64_t kHardcodedPresentationIntervalNanos = 16'666'667;

  Display(uint32_t width, uint32_t height, float device_pixel_ratio);

  // Obtain the time of the last Vsync, in nanoseconds.
  uint64_t GetLastVsyncTime() const;

  // Obtain the interval between Vsyncs.
  uint64_t GetVsyncInterval() const;

  uint32_t width() const { return width_; }
  uint32_t height() const { return height_; }
  float device_pixel_ratio() const { return device_pixel_ratio_; }

 private:
  uint64_t first_vsync_;
  uint32_t width_;
  uint32_t height_;
  float device_pixel_ratio_;
};

}  // namespace scene_manager
