// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/root_presenter/displays/display_configuration.h"

#include "garnet/public/lib/fxl/logging.h"

namespace root_presenter {
namespace display_configuration {

void LogDisplayMetrics(const DisplayMetrics& metrics);

float LookupPixelDensityForDisplay(uint32_t width_in_px, uint32_t height_in_px);
fuchsia::ui::policy::DisplayUsage LookupDisplayUsageForDisplay(
    uint32_t width_in_px, uint32_t height_in_px);

void InitializeModelForDisplay(uint32_t width_in_px, uint32_t height_in_px,
                               DisplayModel* model) {
  FXL_DCHECK(width_in_px != 0u);
  FXL_DCHECK(height_in_px != 0u);
  FXL_DCHECK(model != nullptr);

  model->display_info().width_in_px = width_in_px;
  model->display_info().height_in_px = height_in_px;

  model->display_info().density_in_px_per_mm =
      LookupPixelDensityForDisplay(width_in_px, height_in_px);
  model->environment_info().usage =
      LookupDisplayUsageForDisplay(width_in_px, height_in_px);

  FXL_DCHECK(model->display_info().density_in_px_per_mm != 0.f);
  FXL_DCHECK(model->environment_info().usage !=
             fuchsia::ui::policy::DisplayUsage::kUnknown);
}

// Returns density_in_px_per_mm. This will be replaced by something that
// talks to the display API.
float LookupPixelDensityForDisplay(uint32_t width_in_px,
                                   uint32_t height_in_px) {
  // TODO(MZ-16): Need to have a database of devices and a more robust way
  // of identifying and classifying them.
  if (width_in_px == 2160 && height_in_px == 1440) {
    // Assume that the device is an Acer Switch 12 Alpha.
    FXL_LOG(INFO)
        << "SceneManager: treating device as an Acer Switch 12 Alpha.";
    return 8.5f;
  } else if (width_in_px == 2400 && height_in_px == 1600) {
    // Assume that the device is a Google Pixelbook.
    FXL_LOG(INFO) << "SceneManager: treating device as a Google Pixelbook.";
    return 9.252f;
  } else if (width_in_px == 3840 && height_in_px == 2160) {
    // Assume the display is a 24in 4K monitor.
    FXL_LOG(INFO) << "SceneManager: treating display as a 24in 4K monitor.";
    return 7.323761f;
  } else {
    // TODO(MZ-384): Don't lie.
    FXL_LOG(WARNING) << "SceneManager: unrecognized display.";
    return 9.f;
  }
}

fuchsia::ui::policy::DisplayUsage LookupDisplayUsageForDisplay(
    uint32_t width_in_px, uint32_t height_in_px) {
  // TODO(MZ-16): Need to have a database of devices and a more robust way
  // of identifying and classifying them.
  if (width_in_px == 2160 && height_in_px == 1440) {
    // Assume that the device is an Acer Switch 12 Alpha.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (width_in_px == 2400 && height_in_px == 1600) {
    // Assume that the device is a Google Pixelbook.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (width_in_px == 3840 && height_in_px == 2160) {
    // Assume the display is a 24in 4K monitor.
    return fuchsia::ui::policy::DisplayUsage::kNear;
  } else {
    // TODO(MZ-384): Don't lie.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  }
}

void LogDisplayMetrics(const DisplayMetrics& metrics) {
  FXL_DLOG(INFO) << "SceneManager: Display metrics: "
                 << "width_in_px=" << metrics.width_in_px()
                 << ", height_in_px=" << metrics.height_in_px()
                 << ", width_in_pp=" << metrics.width_in_pp()
                 << ", height_in_pp=" << metrics.height_in_pp()
                 << ", width_in_mm=" << metrics.width_in_mm()
                 << ", height_in_mm=" << metrics.height_in_mm()
                 << ", x_scale_in_px_per_pp=" << metrics.x_scale_in_px_per_pp()
                 << ", y_scale_in_px_per_pp=" << metrics.y_scale_in_px_per_pp()
                 << ", x_scale_in_pp_per_px=" << metrics.x_scale_in_pp_per_px()
                 << ", y_scale_in_pp_per_px=" << metrics.y_scale_in_pp_per_px()
                 << ", density_in_pp_per_mm=" << metrics.density_in_pp_per_mm()
                 << ", density_in_mm_per_pp=" << metrics.density_in_mm_per_pp();
}

}  // namespace display_configuration
}  // namespace root_presenter
