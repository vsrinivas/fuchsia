// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/virtualization/scenic_wayland_dispatcher.h>

namespace guest {

void ScenicWaylandDispatcher::Connect(zx::channel channel) {
  GetOrStartBridge()->Connect(std::move(channel));
}

fuchsia::wayland::Server* ScenicWaylandDispatcher::GetOrStartBridge() {
  if (!wayland_server_) {
    // Launch the bridge process.
    zx::channel request;
    auto services = sys::ServiceDirectory::CreateWithRequest(&request);
    fuchsia::sys::LaunchInfo launch_info{
        .url = bridge_package_url_,
        .directory_request = std::move(request),
    };
    ConnectToLauncher()->CreateComponent(std::move(launch_info), bridge_.NewRequest());
    // If we hit an error just close the bridge. It will get relaunched in
    // response to the next new connection.
    bridge_.set_error_handler(fit::bind_member(this, &ScenicWaylandDispatcher::Reset));
    wayland_server_.set_error_handler(fit::bind_member(this, &ScenicWaylandDispatcher::Reset));

    // Connect to the |WaylandDispatcher| FIDL interface and forward the
    // channel along.
    services->Connect(wayland_server_.NewRequest());
  }

  return wayland_server_.get();
}

void ScenicWaylandDispatcher::Reset(zx_status_t status) {
  FX_LOGS(ERROR) << "Wayland bridge lost: " << status;
  if (bridge_) {
    bridge_.Unbind();
  }
  if (wayland_server_) {
    wayland_server_.Unbind();
  }
}

fuchsia::sys::LauncherPtr ScenicWaylandDispatcher::ConnectToLauncher() const {
  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  return launcher;
}

}  // namespace guest
