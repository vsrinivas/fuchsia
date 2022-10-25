// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/lib/display/get_hardware_display_controller.h"

#include <lib/fdio/directory.h>
#include <lib/fpromise/bridge.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/trace/event.h>
#include <zircon/status.h>

#include <memory>

#include "src/lib/files/directory.h"
#include "src/ui/lib/display/hardware_display_controller_provider_impl.h"

namespace ui_display {

fpromise::promise<DisplayControllerHandles> GetHardwareDisplayController(
    std::shared_ptr<fuchsia::hardware::display::ProviderPtr> provider) {
  zx::channel ctrl_server, ctrl_client;
  zx_status_t status = zx::channel::create(0, &ctrl_server, &ctrl_client);
  if (status != ZX_OK) {
    FX_LOGS(ERROR) << "Failed to create controller channel: " << zx_status_get_string(status);
    return fpromise::make_ok_promise(DisplayControllerHandles{});
  }

  // A reference to |provider| is retained in the closure, to keep the connection open until the
  // response is received.
  fpromise::bridge<DisplayControllerHandles> dc_handles_bridge;
  (*provider)->OpenController(
      ::fidl::InterfaceRequest<fuchsia::hardware::display::Controller>(std::move(ctrl_server)),
      [provider, completer = std::move(dc_handles_bridge.completer),
       ctrl_client = std::move(ctrl_client)](zx_status_t status) mutable {
        if (status != ZX_OK) {
          FX_LOGS(ERROR) << "GetHardwareDisplayController() provider responded with status: "
                         << zx_status_get_string(status);
          completer.complete_ok(DisplayControllerHandles{});
          return;
        }

        DisplayControllerHandles handles{
            ::fidl::InterfaceHandle<fuchsia::hardware::display::Controller>(
                std::move(ctrl_client))};
        completer.complete_ok(std::move(handles));
      });

  return dc_handles_bridge.consumer.promise();
}

fpromise::promise<DisplayControllerHandles> GetHardwareDisplayController(
    ui_display::HardwareDisplayControllerProviderImpl* hdcp_service_impl) {
  TRACE_DURATION("gfx", "GetHardwareDisplayController");

  // We check the environment to see if there is any fake display is provided through
  // fuchsia.hardware.display.Provider protocol. We connect to fake display if given. Otherwise, we
  // fall back to |hdcp_service_impl|.
  // TODO(fxbug.dev/73816): Change fake display injection after moving to CFv2.
  std::vector<std::string> contents;
  files::ReadDirContents("/svc", &contents);
  const bool fake_display_is_injected =
      std::find(contents.begin(), contents.end(), "fuchsia.hardware.display.Provider") !=
      contents.end();

  auto provider = std::make_shared<fuchsia::hardware::display::ProviderPtr>();
  if (fake_display_is_injected) {
    const char* kSvcPath = "/svc/fuchsia.hardware.display.Provider";
    zx_status_t status =
        fdio_service_connect(kSvcPath, provider->NewRequest().TakeChannel().release());
    if (status != ZX_OK) {
      FX_LOGS(ERROR) << "GetHardwareDisplayController() failed to connect to " << kSvcPath
                     << " with status: " << zx_status_get_string(status)
                     << ". Something went wrong in fake-display injection routing.";
      return fpromise::make_result_promise<DisplayControllerHandles>(fpromise::error());
    }
  } else if (hdcp_service_impl) {
    hdcp_service_impl->BindDisplayProvider(provider->NewRequest());
  } else {
    FX_LOGS(ERROR) << "No display provider given.";
    return fpromise::make_result_promise<DisplayControllerHandles>(fpromise::error());
  }
  return GetHardwareDisplayController(std::move(provider));
}

}  // namespace ui_display
