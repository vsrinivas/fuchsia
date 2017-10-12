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
      [ this, callback = std::move(callback) ](const DisplayMetrics* metrics) {
        if (metrics) {
          CreateDefaultDisplay(metrics);
        }
        callback();
      });
}

void DisplayManager::CreateDefaultDisplay(const DisplayMetrics* metrics) {
  uint32_t multiple = Renderer::kRequiredSwapchainPixelMultiple;

  // TODO(MZ-16): We shouldn't be mangling the metrics like this.
  // Ideally the minimum alignment should be handled by the renderer itself.

  uint32_t width = metrics->width_in_px();
  uint32_t height = metrics->height_in_px();
  if (width % multiple != 0u) {
    // Round up to the nearest multiple.
    uint32_t new_width = multiple * (width / multiple) + multiple;
    FXL_LOG(WARNING) << "Mozart SceneManager: Screen width " << width
                     << " is not a multiple of " << multiple
                     << ", rounding up to " << new_width << ".";
    width = new_width;
  }

  if (height % multiple != 0u) {
    // Round up to the nearest multiple.
    uint32_t new_height = multiple * (height / multiple) + multiple;
    FXL_LOG(WARNING) << "Mozart SceneManager: Screen width " << height
                     << " is not a multiple of " << multiple
                     << ", rounding up to " << new_height << ".";
    height = new_height;
  }

  default_display_ = std::make_unique<Display>(DisplayMetrics(
      width, height, metrics->x_scale_in_px_per_gr(),
      metrics->y_scale_in_px_per_gr(), metrics->density_in_gr_per_mm()));
}

}  // namespace scene_manager
