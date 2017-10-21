// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display_manager.h"

#include <zircon/syscalls.h>

#include "garnet/bin/ui/scene_manager/displays/display_watcher.h"
#include "garnet/bin/ui/scene_manager/resources/renderers/renderer.h"

namespace scene_manager {

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() = default;

void DisplayManager::WaitForDefaultDisplay(fxl::Closure callback) {
  FXL_DCHECK(!default_display_);

  display_watcher_.WaitForDisplay(
      [this, callback = std::move(callback)](const DisplayMetrics* metrics) {
        if (metrics) {
          CreateDefaultDisplay(metrics);
        }
        callback();
      });
}

void DisplayManager::CreateDefaultDisplay(const DisplayMetrics* metrics) {
  default_display_ = std::make_unique<Display>(DisplayMetrics(
      metrics->width_in_px(), metrics->height_in_px(),
      metrics->x_scale_in_px_per_gr(), metrics->y_scale_in_px_per_gr(),
      metrics->density_in_gr_per_mm()));
}

}  // namespace scene_manager
