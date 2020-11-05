// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/hardware_display_controller_provider/fake/service.h"

#include <lib/async/cpp/task.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/syslog/cpp/macros.h>

#include "src/graphics/display/drivers/fake/sysmem-proxy-device.h"

namespace fake_display {

ProviderService::ProviderService(sys::ComponentContext* app_context, async_dispatcher_t* dispatcher)
    : dispatcher_(dispatcher) {
  FX_DCHECK(dispatcher_);

  app_context->outgoing()->AddPublicService(bindings_.GetHandler(this));

  auto sysmem = std::make_unique<display::GenericSysmemDeviceWrapper<display::SysmemProxyDevice>>();
  tree_ = std::make_unique<display::FakeDisplayDeviceTree>(std::move(sysmem), /*start_vsync=*/true);
}

void ProviderService::OpenController(
    zx::channel device,
    ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
    OpenControllerCallback callback) {
  callback(ConnectClient(/*is_virtcon=*/false, std::move(device), std::move(controller_request)));
}

void ProviderService::OpenVirtconController(
    zx::channel device,
    ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller_request,
    OpenControllerCallback callback) {
  callback(ConnectClient(/*is_virtcon=*/true, std::move(device), std::move(controller_request)));
}

zx_status_t ProviderService::ConnectClient(
    bool is_virtcon, zx::channel device,
    ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller> controller) {
  auto& num_clients = is_virtcon ? num_virtcon_clients_ : num_clients_;
  if (num_clients++ > 0) {
    // If this occurs then test tried to connect a client before the previous client's channel had
    // was destroyed.  It may prove desirable to defer the latter connection until the previous one
    // has been torn down, but for now we just log an error (to help decipher test failures, should
    // they occur).
    FX_LOGS(ERROR) << "Multiple simultaneous display controller connections.  Most recent one will "
                      "likely fail.";
  }

  return tree_->controller()->CreateClient(is_virtcon, std::move(device), controller.TakeChannel(),
                                           [&num_clients]() mutable { --num_clients; });
}

}  // namespace fake_display
