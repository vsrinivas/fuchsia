// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/root_presenter/displays/display_configuration.h"

#include <lib/syslog/cpp/macros.h>

#include "src/lib/files/file.h"
#include "src/lib/fxl/strings/trim.h"

namespace root_presenter {
namespace display_configuration {

void LogDisplayMetrics(const DisplayMetrics& metrics);

float LookupPixelDensityForDisplay(uint32_t width_in_px, uint32_t height_in_px);
fuchsia::ui::policy::DisplayUsage LookupDisplayUsageForDisplay(uint32_t width_in_px,
                                                               uint32_t height_in_px);

void InitializeModelForDisplay(uint32_t width_in_px, uint32_t height_in_px, DisplayModel* model) {
  FX_DCHECK(width_in_px != 0u);
  FX_DCHECK(height_in_px != 0u);
  FX_DCHECK(model != nullptr);

  model->display_info().width_in_px = width_in_px;
  model->display_info().height_in_px = height_in_px;

  model->display_info().density_in_px_per_mm =
      LookupPixelDensityForDisplay(width_in_px, height_in_px);
  model->environment_info().usage = LookupDisplayUsageForDisplay(width_in_px, height_in_px);

  FX_DCHECK(model->display_info().density_in_px_per_mm != 0.f);
  FX_DCHECK(model->environment_info().usage != fuchsia::ui::policy::DisplayUsage::kUnknown);
}

// Returns density_in_px_per_mm. This will be replaced by something that
// talks to the display API.
float LookupPixelDensityForDisplay(uint32_t width_in_px, uint32_t height_in_px) {
  {
    std::string pixel_density;
    if (files::ReadFileToString("/config/data/display_pixel_density", &pixel_density)) {
      auto pixel_density_value = atof(pixel_density.c_str());
      if (pixel_density_value != 0.0) {
        FX_LOGS(INFO) << "Display pixel density applied: " << pixel_density_value << " px/mm.";
        return pixel_density_value;
      } else {
        FX_LOGS(WARNING) << "Invalid display pixel density in configuration: " << pixel_density
                         << " px/mm.";
      }
    }
  }

  // TODO(SCN-16): Need to have a database of devices and a more robust way
  // of identifying and classifying them.
  if (width_in_px == 2160 && height_in_px == 1440) {
    // Assume that the device is an Acer Switch 12 Alpha.
    FX_LOGS(INFO) << "RootPresenter: treating device as an Acer Switch 12 Alpha.";
    return 8.5f;
  } else if (width_in_px == 2400 && height_in_px == 1600) {
    // Assume that the device is a Google Pixelbook.
    FX_LOGS(INFO) << "RootPresenter: treating device as a Google Pixelbook.";
    return 9.252f;
  } else if (width_in_px == 3840 && height_in_px == 2160) {
    // Assume the display is a 24in 4K monitor.
    FX_LOGS(INFO) << "RootPresenter: treating display as a 24in 4K monitor.";
    return 7.323761f;
  } else if (width_in_px == 1920 && height_in_px == 1200) {
    // Assume the display is a 24in HD monitor.
    FX_LOGS(INFO) << "RootPresenter: treating display as a 24in monitor.";
    return 4.16f;
  } else if (width_in_px == 2560 && height_in_px == 1440) {
    // TODO(fxbug.dev/42794): Allow Root Presenter clients to specify exact pixel ratio
    // Assume display is a 27in 2K monitor.
    FX_LOGS(INFO) << "RootPresenter: treating device as a 27in 2k monitor.";
    return 5.22f;
  } else {
    // TODO(SCN-384): Don't lie.
    FX_LOGS(WARNING) << "RootPresenter: unrecognized display.";
    return 9.f;
  }
}

fuchsia::ui::policy::DisplayUsage LookupDisplayUsageForDisplay(uint32_t width_in_px,
                                                               uint32_t height_in_px) {
  // TODO(SCN-16): Need to have a database of devices and a more robust way
  // of identifying and classifying them.
  {
    std::string raw_display_usage;
    if (files::ReadFileToString("/config/data/display_usage", &raw_display_usage)) {
      std::string_view display_usage = fxl::TrimString(raw_display_usage, "\n ");
      if (display_usage == "handheld") {
        return fuchsia::ui::policy::DisplayUsage::kHandheld;
      } else if (display_usage == "close") {
        return fuchsia::ui::policy::DisplayUsage::kClose;
      } else if (display_usage == "near") {
        return fuchsia::ui::policy::DisplayUsage::kNear;
      } else if (display_usage == "midrange") {
        return fuchsia::ui::policy::DisplayUsage::kMidrange;
      } else if (display_usage == "far") {
        return fuchsia::ui::policy::DisplayUsage::kFar;
      } else {
        FX_LOGS(WARNING) << "Invalid display usage in configuration: " << display_usage << ".";
      }
    }
  }

  if (width_in_px == 2160 && height_in_px == 1440) {
    // Assume that the device is an Acer Switch 12 Alpha.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (width_in_px == 2400 && height_in_px == 1600) {
    // Assume that the device is a Google Pixelbook.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  } else if (width_in_px == 3840 && height_in_px == 2160) {
    // Assume the display is a 24in 4K monitor.
    return fuchsia::ui::policy::DisplayUsage::kNear;
  } else if (width_in_px == 1920 && height_in_px == 1200) {
    // Assume the display is a 24in monitor.
    return fuchsia::ui::policy::DisplayUsage::kNear;
  } else {
    // TODO(SCN-384): Don't lie.
    return fuchsia::ui::policy::DisplayUsage::kClose;
  }
}

void LogDisplayMetrics(const DisplayMetrics& metrics) {
  FX_DLOGS(INFO) << "RootPresenter: Display metrics: "
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
