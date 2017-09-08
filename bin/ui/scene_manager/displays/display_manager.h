// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <cstdint>

#include "apps/mozart/src/scene_manager/displays/display.h"
#include "apps/mozart/src/scene_manager/displays/display_watcher.h"
#include "lib/ftl/functional/closure.h"
#include "lib/ftl/macros.h"

namespace scene_manager {

// Provides support for enumerating available displays.
class DisplayManager {
 public:
  DisplayManager();
  ~DisplayManager();

  // Waits for the default display to become available then invokes the
  // callback.
  void WaitForDefaultDisplay(ftl::Closure callback);

  // Gets information about the default display.
  // May return null if there isn't one.
  Display* default_display() const { return default_display_.get(); }

  // For testing.
  void SetDefaultDisplayForTests(std::unique_ptr<Display> display) {
    default_display_ = std::move(display);
  }

 private:
  void CreateDefaultDisplay(uint32_t width,
                            uint32_t height,
                            float device_pixel_ratio);

  DisplayWatcher display_watcher_;
  std::unique_ptr<Display> default_display_;

  FTL_DISALLOW_COPY_AND_ASSIGN(DisplayManager);
};

}  // namespace scene_manager
