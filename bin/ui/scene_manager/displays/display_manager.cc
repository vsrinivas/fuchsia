// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/ui/scene_manager/displays/display_manager.h"

#include <magenta/syscalls.h>

#include "garnet/bin/ui/scene_manager/displays/display_watcher.h"
#include "garnet/bin/ui/scene_manager/resources/renderers/renderer.h"

namespace scene_manager {

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() = default;

void DisplayManager::WaitForDefaultDisplay(ftl::Closure callback) {
  FTL_DCHECK(!default_display_);

  display_watcher_.WaitForDisplay([ this, callback = std::move(callback) ](
      bool success, uint32_t width, uint32_t height, float device_pixel_ratio) {
    if (success) {
      CreateDefaultDisplay(width, height, device_pixel_ratio);
    }
    callback();
  });
}

void DisplayManager::CreateDefaultDisplay(uint32_t width,
                                          uint32_t height,
                                          float device_pixel_ratio) {
  uint32_t multiple = Renderer::kRequiredSwapchainPixelMultiple;
  if (width % multiple != 0) {
    // Round up to the nearest multiple.
    uint32_t new_width = multiple * (width / multiple) + multiple;
    FTL_LOG(WARNING) << "Mozart SceneManager: Screen width " << width
                     << " is not a multiple of " << multiple
                     << ", rounding up to " << new_width << ".";
    width = new_width;
  }

  if (height % multiple != 0) {
    // Round up to the nearest multiple.
    uint32_t new_height = multiple * (height / multiple) + multiple;
    FTL_LOG(WARNING) << "Mozart SceneManager: Screen width " << height
                     << " is not a multiple of " << multiple
                     << ", rounding up to " << new_height << ".";
    height = new_height;
  }

  default_display_ =
      std::make_unique<Display>(width, height, device_pixel_ratio);
}

}  // namespace scene_manager
