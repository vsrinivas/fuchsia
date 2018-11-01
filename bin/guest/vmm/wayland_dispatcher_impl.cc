// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/vmm/wayland_dispatcher_impl.h"

#include <lib/fxl/logging.h>

static constexpr char kWaylandDispatcherPackage[] = "wayland_bridge";

void WaylandDispatcherImpl::OnNewConnection(zx::channel channel) {
  std::lock_guard lock(mutex_);
  GetOrStartBridgeLocked()->OnNewConnection(std::move(channel));
}

fuchsia::guest::WaylandDispatcher*
WaylandDispatcherImpl::GetOrStartBridgeLocked() {
  if (!dispatcher_) {
    // Launch the bridge process.
    component::Services services;
    fuchsia::sys::LaunchInfo launch_info{
        .url = kWaylandDispatcherPackage,
        .directory_request = services.NewRequest(),
    };
    launcher_->CreateComponent(std::move(launch_info), bridge_.NewRequest());
    bridge_.set_error_handler([this](zx_status_t status) {
      std::lock_guard lock(mutex_);
      bridge_ = nullptr;
      dispatcher_ = nullptr;
    });
    // Connect to the |WaylandDispatcher| FIDL interface and forward the
    // channel along.
    services.ConnectToService(dispatcher_.NewRequest());
  }

  return dispatcher_.get();
}
