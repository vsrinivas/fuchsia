// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display_configuration.h"

#include "garnet/public/lib/fxl/logging.h"

namespace scene_manager {

void ConfigureDisplay(uint32_t width_in_px,
                      uint32_t height_in_px,
                      DisplayModel* model) {
  FXL_DCHECK(width_in_px != 0u);
  FXL_DCHECK(height_in_px != 0u);
  FXL_DCHECK(model != nullptr);

  model->display_info().width_in_px = width_in_px;
  model->display_info().height_in_px = height_in_px;

  // TODO(MZ-16): Need to have a database of devices and a more robust way
  // of identifying and classifying them.
  if (width_in_px == 2160 && height_in_px == 1440) {
    // Assume that the device is an Acer Switch 12 Alpha.
    FXL_LOG(INFO)
        << "SceneManager: treating device as an Acer Switch 12 Alpha.";
    model->display_info().density_in_px_per_mm = 8.386f;
    model->environment_info().usage = DisplayModel::Usage::kClose;
  } else if (width_in_px == 2400 && height_in_px == 1600) {
    // Assume that the device is a Google Pixelbook.
    FXL_LOG(INFO) << "SceneManager: treating device as a Google Pixelbook.";
    model->display_info().density_in_px_per_mm = 9.252f;
    model->environment_info().usage = DisplayModel::Usage::kClose;
  } else {
    // TODO(MZ-384): Don't lie.
    FXL_LOG(WARNING) << "SceneManager: unrecognized display.";
    model->display_info().density_in_px_per_mm = 9.f;
    model->environment_info().usage = DisplayModel::Usage::kClose;
  }
}

}  // namespace scene_manager
