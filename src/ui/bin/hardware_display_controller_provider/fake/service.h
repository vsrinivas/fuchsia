// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_
#define SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_

#include <fuchsia/hardware/display/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>

#include <memory>

#include "src/graphics/display/drivers/fake/fake-display-device-tree.h"

namespace sys {
class ComponentContext;
}  // namespace sys

namespace fake_display {

class ProviderService : public fuchsia::hardware::display::Provider {
 public:
  // |app_context| is used to publish this service.
  ProviderService(sys::ComponentContext* app_context, async_dispatcher_t* dispatcher);

  // |fuchsia::hardware::display::Provider|.
  void OpenVirtconController(
      zx::channel device,
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
      OpenVirtconControllerCallback callback) override;

  // |fuchsia::hardware::display::Provider|.
  void OpenController(
      zx::channel device,
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
      OpenControllerCallback callback) override;

 private:
  zx_status_t ConnectClient(
      bool is_virtcon, zx::channel device,
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller);

  async_dispatcher_t* const dispatcher_;

  fidl::BindingSet<fuchsia::hardware::display::Provider> bindings_;

  std::unique_ptr<display::FakeDisplayDeviceTree> tree_;
  uint32_t num_clients_ = 0;
  uint32_t num_virtcon_clients_ = 0;
};

}  // namespace fake_display

#endif  // SRC_UI_BIN_HARDWARE_DISPLAY_CONTROLLER_PROVIDER_FAKE_SERVICE_H_
