// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_
#define LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/wayland/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

namespace guest {

// Provides a |WaylandDispatcher| that will create a scenic view for each
// wayland shell surface.
//
// This class is not thread-safe.
class ScenicWaylandDispatcher : public fuchsia::wayland::Server {
 public:
  ScenicWaylandDispatcher(sys::ComponentContext* context, const char* bridge_package_url)
      : context_(context), bridge_package_url_(bridge_package_url) {}

  // |fuchsia::wayland::Server|
  void Connect(zx::channel channel);

  fidl::InterfaceHandle<fuchsia::wayland::Server> NewBinding() { return binding_.NewBinding(); }

 private:
  fuchsia::sys::LauncherPtr ConnectToLauncher() const;

  void Reset(zx_status_t status);

  fuchsia::wayland::Server* GetOrStartBridge();

  sys::ComponentContext* context_ = nullptr;
  const char* const bridge_package_url_;

  // Receive a new Wayland channel to the virtio_wl device.
  fidl::Binding<fuchsia::wayland::Server> binding_{this};

  // Management of the `wayland_bridge` component.
  fuchsia::sys::ComponentControllerPtr bridge_;
  // Client endpoint to `wayland_bridge`; for forwarding the Wayland channel.
  fuchsia::wayland::ServerPtr wayland_server_;
};

}  // namespace guest

#endif  // LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_
