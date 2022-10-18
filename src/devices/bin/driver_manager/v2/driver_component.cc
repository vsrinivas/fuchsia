// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/devices/bin/driver_manager/v2/driver_component.h"

#include "src/devices/lib/log/log.h"

namespace fdh = fuchsia_driver_host;

namespace dfv2 {

DriverComponent::DriverComponent(
    fidl::ClientEnd<fdh::Driver> driver,
    fidl::ServerEnd<fuchsia_component_runner::ComponentController> component,
    async_dispatcher_t* dispatcher, std::string_view url, RequestRemoveCallback request_remove,
    RemoveCallback remove)
    : driver_(std::move(driver), dispatcher, this),
      url_(url),
      request_remove_(std::move(request_remove)),
      remove_(std::move(remove)) {
  driver_ref_ = fidl::BindServer(dispatcher, std::move(component), this,
                                 [](DriverComponent* driver, auto, auto) {
                                   driver->is_alive_ = false;
                                   driver->remove_(ZX_OK);
                                 });
}

void DriverComponent::RequestDriverStop() { request_remove_(ZX_OK); }

zx_status_t DriverComponent::StopDriver() {
  if (stop_in_progress_) {
    return ZX_OK;
  }

  auto result = driver_->Stop();
  if (!result.ok()) {
    LOGF(ERROR, "Failed to stop a driver: %s", result.FormatDescription().data());
    return result.status();
  }
  stop_in_progress_ = true;
  return ZX_OK;
}

void DriverComponent::on_fidl_error(fidl::UnbindInfo info) {
  // The only valid way a driver host should shut down the Driver channel
  // is with the ZX_OK epitaph.
  if (info.reason() != fidl::Reason::kPeerClosed || info.status() != ZX_OK) {
    LOGF(ERROR, "DriverComponent: %s: driver channel shutdown with: %s", url_.data(),
         info.FormatDescription().data());
  }

  // We are disconnected from the DriverHost so shut everything down.
  StopComponent();
}

void DriverComponent::Stop(DriverComponent::StopCompleter::Sync& completer) { RequestDriverStop(); }

void DriverComponent::Kill(DriverComponent::KillCompleter::Sync& completer) { RequestDriverStop(); }

void DriverComponent::StopComponent() {
  if (!driver_ref_) {
    return;
  }
  // Send an epitaph to the component manager and close the connection. The
  // server of a `ComponentController` protocol is expected to send an epitaph
  // before closing the associated connection.
  driver_ref_->Close(ZX_OK);
  driver_ref_.reset();
}

}  // namespace dfv2
