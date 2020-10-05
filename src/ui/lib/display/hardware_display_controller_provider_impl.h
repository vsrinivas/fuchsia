// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_LIB_DISPLAY_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_IMPL_H_
#define SRC_UI_LIB_DISPLAY_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_IMPL_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <cstdint>
#include <map>
#include <memory>

#include "src/lib/fsl/io/device_watcher.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace ui_display {

// Implements the FIDL fuchsia.hardware.display.Provider API.  Only provides access to the primary
// controller
class HardwareDisplayControllerProviderImpl : public fuchsia::hardware::display::Provider {
 public:
  // |app_context| is used to publish this service.
  HardwareDisplayControllerProviderImpl(sys::ComponentContext* app_context);

  // |fuchsia::hardware::display::Provider|.
  void OpenVirtconController(
      zx::channel device,
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller,
      OpenVirtconControllerCallback callback) override {
    callback(ZX_ERR_NOT_SUPPORTED);
  }

  // |fuchsia::hardware::display::Provider|.
  void OpenController(zx::channel device,
                      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller,
                      OpenControllerCallback callback) override;

 private:
  fidl::BindingSet<fuchsia::hardware::display::Provider> bindings_;

  // The currently outstanding DeviceWatcher closures.  The closures will remove
  // themselves from here if they are invoked before shutdown.  Any closures
  // still outstanding will be handled by the destructor.
  // This approach assumes that the event loop is attached to
  // the main thread, else race conditions may occur.
  std::map<uint64_t, std::unique_ptr<fsl::DeviceWatcher>> holders_;
};

}  // namespace ui_display

#endif  // SRC_UI_LIB_DISPLAY_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_IMPL_H_
