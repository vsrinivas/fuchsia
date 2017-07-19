// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/mozart/src/scene/display.h"

#include <magenta/syscalls.h>

namespace mozart {
namespace scene {

Display::Display(uint32_t width, uint32_t height, float device_pixel_ratio)
    : first_vsync_(mx_time_get(MX_CLOCK_MONOTONIC)),
      width_(width),
      height_(height),
      device_pixel_ratio_(device_pixel_ratio) {}

uint64_t Display::GetLastVsyncTime() const {
  uint64_t current_time = mx_time_get(MX_CLOCK_MONOTONIC);
  uint64_t num_elapsed_intervals =
      (current_time - first_vsync_) / kHardcodedPresentationIntervalNanos;
  return first_vsync_ +
         num_elapsed_intervals * kHardcodedPresentationIntervalNanos;
}

uint64_t Display::GetVsyncInterval() const {
  return kHardcodedPresentationIntervalNanos;
}

}  // namespace scene
}  // namespace mozart
