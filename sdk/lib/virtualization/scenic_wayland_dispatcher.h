// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_
#define LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_

#include <fuchsia/virtualization/cpp/fidl.h>
#include <fuchsia/wayland/cpp/fidl.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/sys/cpp/component_context.h>
#include <lib/zx/channel.h>

namespace guest {

// Provides a |WaylandDispatcher| that will create a scenic view for each
// wayland shell surface.
//
// This class is not thread-safe.
class ScenicWaylandDispatcher : public fuchsia::virtualization::WaylandDispatcher {
 public:
  using ViewListener =
      fit::function<void(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>, uint32_t)>;
  using ShutdownViewListener = fit::function<void(uint32_t)>;

  explicit ScenicWaylandDispatcher(sys::ComponentContext* context, ViewListener listener = nullptr,
                                   ShutdownViewListener shutdown_listener = nullptr)
      : context_(context),
        listener_(std::move(listener)),
        shutdown_listener_(std::move(shutdown_listener)) {}

  // |fuchsia::virtualization::WaylandDispatcher|
  void OnNewConnection(zx::channel channel);

  fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher> NewBinding() {
    return binding_.NewBinding();
  }

 private:
  fuchsia::sys::LauncherPtr ConnectToLauncher() const;

  void OnNewView1(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view);
  void OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view, uint32_t id);
  void OnShutdownView(uint32_t id);
  void Reset(zx_status_t status);

  fuchsia::virtualization::WaylandDispatcher* GetOrStartBridge();

  sys::ComponentContext* context_ = nullptr;

  // Constructor-defined behaviors.
  ViewListener listener_;
  ShutdownViewListener shutdown_listener_;

  // Receive a new Wayland channel to the virtio_wl device.
  fidl::Binding<fuchsia::virtualization::WaylandDispatcher> binding_{this};

  // Management of the `wayland_bridge` component.
  fuchsia::sys::ComponentControllerPtr bridge_;
  // Client endpoint to `wayland_bridge`; for forwarding the Wayland channel.
  fuchsia::virtualization::WaylandDispatcherPtr dispatcher_;
  // Client endpoint to `wayland_bridge`; receive Scenic view lifecycle events.
  fuchsia::wayland::ViewProducerPtr view_producer_;
};

};  // namespace guest

#endif  // LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_
