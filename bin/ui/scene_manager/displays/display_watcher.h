// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <memory>

#include "garnet/bin/ui/scene_manager/displays/display_metrics.h"
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
      std::function<void(const DisplayMetrics* metrics)>;

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
