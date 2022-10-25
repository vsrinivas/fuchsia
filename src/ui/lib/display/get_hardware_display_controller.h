// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_
#define SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fpromise/promise.h>

namespace ui_display {

class HardwareDisplayControllerProviderImpl;

struct DisplayControllerHandles {
  fidl::InterfaceHandle<fuchsia::hardware::display::Controller> controller;
};

// Connect to the fuchsia::hardware::display::Provider service, and return a promise which will be
// resolved when the display controller is obtained. One variant uses the explicitly-provided
// service, and the other variant finds the service in the component's environment.
//
// If the display controller cannot be obtained for some reason, |hdcp_service_impl| will be used to
// bind a connection if given. Otherwise, the handles will be null.
//
// |hdcp_service_impl| binding to Display is done internally and does not need any published
// services. This breaks the dependency in Scenic service startup.
fpromise::promise<DisplayControllerHandles> GetHardwareDisplayController(
    std::shared_ptr<fuchsia::hardware::display::ProviderPtr> provider);
fpromise::promise<DisplayControllerHandles> GetHardwareDisplayController(
    HardwareDisplayControllerProviderImpl* hdcp_service_impl = nullptr);

}  // namespace ui_display

#endif  // SRC_UI_LIB_DISPLAY_GET_HARDWARE_DISPLAY_CONTROLLER_H_
