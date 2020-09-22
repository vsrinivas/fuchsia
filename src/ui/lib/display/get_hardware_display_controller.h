// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_
#define SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fit/promise.h>

namespace ui_display {

// TODO(fxbug.dev/33675): 'display-controller.fidl' requires the client to keep |dc_device| open; it is
// otherwise unused.  Eventually, only |controller| will be required.
struct DisplayControllerHandles {
  fidl::InterfaceHandle<fuchsia::hardware::display::Controller> controller;
  zx::channel dc_device;
};

// Connect to the fuchsia::hardware::display::Provider service, and return a promise which will be
// resolved when the display controller is obtained.  One variant uses the explicitly-provided
// service, and the other variant finds the service in the component's environment.
//
// If the display controller cannot be obtained for some reason, the handles will be null.
fit::promise<DisplayControllerHandles> GetHardwareDisplayController(
    std::shared_ptr<fuchsia::hardware::display::ProviderPtr> provider);
fit::promise<DisplayControllerHandles> GetHardwareDisplayController();

}  // namespace ui_display

#endif  // SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_
