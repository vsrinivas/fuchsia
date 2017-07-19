// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#pragma once

#include <magenta/device/display.h>
#include <magenta/pixelformat.h>
#include <mx/vmo.h>

#include <memory>

#include "lib/ftl/macros.h"
#include "lib/mtl/io/device_watcher.h"

namespace scene_manager {

// Waits for a display device to be available, and returns the display
// attributes through a callback.
class DisplayWatcher {
 public:
  // Callback that accepts a success param, width, height, and a device pixel
  // ratio.
  // |success| is true if the display was acquired and the display info was
  // read, or false otherwise.
  using OnDisplayReady = std::function<void(bool, uint32_t, uint32_t, float)>;

  // Creates a DisplayWatcher object. |callback| will be invoked once the
  // display is ready. The object must remain alive until the callback is
  // received.
  static std::unique_ptr<DisplayWatcher> New(OnDisplayReady callback);

  ~DisplayWatcher() = default;

 private:
  DisplayWatcher(OnDisplayReady callback);

  void WaitForDisplay();

  OnDisplayReady callback_;
  std::unique_ptr<mtl::DeviceWatcher> device_watcher_;
};

}  // namespace scene_manager
