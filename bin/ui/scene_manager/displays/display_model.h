// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "garnet/bin/ui/scene_manager/displays/display_metrics.h"
#include "lib/fxl/macros.h"

namespace scene_manager {

// Calculates appropriate display metrics based on an empirical model
// using information about the display, the environment, and the user.
//
// Refer to |ui_units_and_metrics.md| for details.
//
// This model currently assumes the entire content area is visible.
// This model currently assumes that pixels are square.
class DisplayModel {
 public:
  // Provides information about the display's physical characteristics.
  struct DisplayInfo {
    // The width of the visible content area in pixels.
    // Must be greater than 0 for metrics calculation.
    uint32_t width_in_px = 0;

    // The height of the visible content area in pixels.
    // Must be greater than 0 for metrics calculation.
    uint32_t height_in_px = 0;

    // The physical width of the visible content area in millimeters.
    // Value is 0.0 if unknown.
    float width_in_mm = 0.f;

    // The physical height of the visible content area in millimeters.
    // Value is 0.0 if unknown.
    float height_in_mm = 0.f;

    // The pixel density of the visible content area in pixels per millimeter.
    // Value is 0.0 if unknown.
    float density_in_px_per_mm = 0.f;
  };

  // Describes the intended usage of the display.
  enum class Usage {
    // Unknown.
    kUnknown = 0,
    // Display is held in one or both hands.
    kHandheld = 1,
    // Display is used well within arm's reach.
    kClose = 2,
    // Display is used at arm's reach.
    kNear = 3,
    // Display is used well beyond arm's reach.
    kFar = 4,
  };

  // Provides information about the viewing environment.
  struct EnvironmentInfo {
    // The intended usage of the display.
    // Value is |kUnknown| if unknown.
    Usage usage = Usage::kUnknown;

    // The nominal apparent viewing distance in millimeters.
    // Value is 0.0 if unknown.
    float viewing_distance_in_mm = 0.f;
  };

  // Provides information about user preferences.
  struct UserInfo {
    // User-specified magnification factor, e.g. for accessibility.
    // Use 1.0 if none.
    float user_scale_factor = 1.f;
  };

  DisplayModel();
  ~DisplayModel();

  // Model parameters.
  DisplayInfo& display_info() { return display_info_; }
  EnvironmentInfo& environment_info() { return environment_info_; }
  UserInfo& user_info() { return user_info_; }

  // Calculates the display metrics.
  DisplayMetrics GetMetrics();

 private:
  DisplayInfo display_info_;
  EnvironmentInfo environment_info_;
  UserInfo user_info_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayModel);
};

}  // namespace scene_manager
