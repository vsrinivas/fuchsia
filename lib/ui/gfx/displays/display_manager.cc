// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/lib/ui/gfx/displays/display_manager.h"

#include <zircon/syscalls.h>

#include "garnet/lib/ui/gfx/displays/display_watcher.h"
#include "garnet/lib/ui/gfx/resources/renderers/renderer.h"

namespace scenic {
namespace gfx {

DisplayManager::DisplayManager() = default;

DisplayManager::~DisplayManager() = default;

void DisplayManager::WaitForDefaultDisplay(fxl::Closure callback) {
  FXL_DCHECK(!default_display_);

  display_watcher_.WaitForDisplay(
      [this, callback = std::move(callback)](uint32_t width_in_px,
                                             uint32_t height_in_px,
                                             zx::event ownership_event) {
        CreateDefaultDisplay(
            width_in_px, height_in_px, std::move(ownership_event));
        callback();
      });
}

void DisplayManager::CreateDefaultDisplay(uint32_t width_in_px,
                                          uint32_t height_in_px,
                                          zx::event ownership_event) {
  default_display_ = std::make_unique<Display>(
      width_in_px, height_in_px, std::move(ownership_event));
}

}  // namespace gfx
}  // namespace scenic
