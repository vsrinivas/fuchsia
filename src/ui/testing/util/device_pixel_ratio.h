// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_TESTING_UTIL_DEVICE_PIXEL_RATIO_H_
#define SRC_UI_TESTING_UTIL_DEVICE_PIXEL_RATIO_H_

#include <string>

// TODO(fxbug.dev/112485): Move these constants to UITestRealm.

namespace ui_testing {

// Display pixel densities for use in tests.

// Display pixel density for a hypothetical "low-resolution" display.
constexpr float kLowResolutionDisplayPixelDensity = 4.1668f;

// Display pixel density for a hypothetical "medium-resolution" display.
constexpr float kMediumResolutionDisplayPixelDensity = 5.2011f;

// Display pixel density for a hypothetical "high-resolution" display.
constexpr float kHighResolutionDisplayPixelDensity = 2 * kLowResolutionDisplayPixelDensity;

// Display usage names.
constexpr auto kDisplayUsageNear = "near";

// Returns the expected pixel scale for a given display density and usage.
float GetExpectedPixelScale(float display_pixel_density, std::string usage);

}  // namespace ui_testing

#endif  // SRC_UI_TESTING_UTIL_DEVICE_PIXEL_RATIO_H_
