// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/testing/util/device_pixel_ratio.h"

#include <lib/syslog/cpp/macros.h>

namespace ui_testing {

float GetExpectedPixelScale(float display_pixel_density, std::string usage) {
  FX_CHECK(usage == kDisplayUsageNear) << "unsupported display usage: " << usage;

  float pixel_scale = 0.f;

  if (display_pixel_density == kLowResolutionDisplayPixelDensity) {
    pixel_scale = 1.f;
  } else if (display_pixel_density == kMediumResolutionDisplayPixelDensity) {
    pixel_scale = 1.f / 1.25f;
  } else if (display_pixel_density == kHighResolutionDisplayPixelDensity) {
    pixel_scale = 0.5f;
  }

  FX_CHECK(pixel_scale != 0.f) << "unsupported display pixel density: " << display_pixel_density;

  return pixel_scale;
}

}  // namespace ui_testing
