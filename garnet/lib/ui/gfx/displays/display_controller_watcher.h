// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_CONTROLLER_WATCHER_H_
#define GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_CONTROLLER_WATCHER_H_

#include <lib/fit/function.h>
#include <lib/fsl/io/device_watcher.h>
#include <src/lib/fxl/macros.h>

#include <memory>

namespace scenic_impl {
namespace gfx {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayControllerWatcher {
 public:
  // Callback provides channels to the display controller device and FIDL
  // interface.
  using DisplayControllerReadyCallback =
      fit::function<void(zx::channel device, zx::channel controller)>;

  DisplayControllerWatcher();
  ~DisplayControllerWatcher();

  // Waits for the display to become available then invokes the callback.
  void WaitForDisplayController(DisplayControllerReadyCallback callback);

 private:
  void HandleDevice(DisplayControllerReadyCallback callback, int dir_fd, std::string filename);

  std::unique_ptr<fsl::DeviceWatcher> device_watcher_;

  FXL_DISALLOW_COPY_AND_ASSIGN(DisplayControllerWatcher);
};

}  // namespace gfx
}  // namespace scenic_impl

#endif  // GARNET_LIB_UI_GFX_DISPLAYS_DISPLAY_CONTROLLER_WATCHER_H_
