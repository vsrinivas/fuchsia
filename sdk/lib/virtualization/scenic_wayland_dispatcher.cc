// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/fit/function.h>
#include <lib/sys/cpp/service_directory.h>
#include <lib/syslog/cpp/macros.h>
#include <lib/virtualization/scenic_wayland_dispatcher.h>

static constexpr char kWaylandDispatcherPackage[] =
#ifdef USE_LEGACY_WAYLAND_BRIDGE
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/legacy_wayland_bridge.cmx";
#else
    "fuchsia-pkg://fuchsia.com/wayland_bridge#meta/wayland_bridge.cmx";
#endif

namespace guest {

void ScenicWaylandDispatcher::OnNewConnection(zx::channel channel) {
  GetOrStartBridge()->OnNewConnection(std::move(channel));
}

fuchsia::virtualization::WaylandDispatcher* ScenicWaylandDispatcher::GetOrStartBridge() {
  if (!dispatcher_) {
    // Launch the bridge process.
    zx::channel request;
    auto services = sys::ServiceDirectory::CreateWithRequest(&request);
    fuchsia::sys::LaunchInfo launch_info{
        .url = kWaylandDispatcherPackage,
        .directory_request = std::move(request),
    };
    ConnectToLauncher()->CreateComponent(std::move(launch_info), bridge_.NewRequest());
    // If we hit an error just close the bridge. It will get relaunched in
    // response to the next new connection.
    bridge_.set_error_handler(fit::bind_member(this, &ScenicWaylandDispatcher::Reset));
    dispatcher_.set_error_handler(fit::bind_member(this, &ScenicWaylandDispatcher::Reset));

    // Connect to the |WaylandDispatcher| FIDL interface and forward the
    // channel along.
    services->Connect(dispatcher_.NewRequest());
    services->Connect(view_producer_.NewRequest());
    view_producer_.events().OnNewView = fit::bind_member(this, &ScenicWaylandDispatcher::OnNewView);
    view_producer_.events().OnShutdownView =
        fit::bind_member(this, &ScenicWaylandDispatcher::OnShutdownView);
  }

  return dispatcher_.get();
}

void ScenicWaylandDispatcher::Reset(zx_status_t status) {
  FX_LOGS(ERROR) << "Wayland bridge lost: " << status;
  if (bridge_) {
    bridge_.Unbind();
  }
  if (dispatcher_) {
    dispatcher_.Unbind();
  }
}

void ScenicWaylandDispatcher::OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view,
                                        uint32_t id) {
  listener_(std::move(view), id);
}

void ScenicWaylandDispatcher::OnShutdownView(uint32_t id) { shutdown_listener_(id); }

fuchsia::sys::LauncherPtr ScenicWaylandDispatcher::ConnectToLauncher() const {
  fuchsia::sys::LauncherPtr launcher;
  context_->svc()->Connect(launcher.NewRequest());
  return launcher;
}

};  // namespace guest
