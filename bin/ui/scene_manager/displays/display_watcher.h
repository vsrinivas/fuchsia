// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_WATCHER_H_
#define GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_WATCHER_H_

#include <memory>

#include "lib/fsl/io/device_watcher.h"
#include "lib/fxl/macros.h"

namespace scene_manager {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayWatcher {
 public:
  // Callback that accepts display metrics.
  // |metrics| may be null if the display was not successfully acquired.
  using DisplayReadyCallback =
      std::function<void(uint32_t width_in_px, uint32_t height_in_px)>;

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

#endif  // GARNET_BIN_UI_SCENE_MANAGER_DISPLAYS_DISPLAY_WATCHER_H_
