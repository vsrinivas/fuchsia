// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "lib/fxl/macros.h"
#include "lib/fsl/io/device_watcher.h"

namespace scene_manager {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayWatcher {
 public:
  // Callback that accepts a success param, width, height, and a device pixel
  // ratio.
  // |success| is true if the display was acquired and the display info was
  // read, or false otherwise.
  using DisplayReadyCallback = std::function<void(bool success,
                                                  uint32_t width,
                                                  uint32_t height,
                                                  float device_pixel_ratio)>;

  DisplayWatcher();
  ~DisplayWatcher();

  // Waits for the display to become available then invokes the callback.
  void WaitForDisplay(DisplayReadyCallback callback);

 private:
  void HandleDevice(DisplayReadyCallback callback,
                    int dir_fd,
                    std::string filename);

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayWatcher);
};

}  // namespace scene_manager
