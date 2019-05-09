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

// Provides a |WaylandDispatcher| that will create scenic views for each
// wayland shell surface.
//
// This class is not thread-safe.
class ScenicWaylandDispatcher
    : public fuchsia::virtualization::WaylandDispatcher {
 public:
  using ViewListener = fit::function<void(
      fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider>)>;

  ScenicWaylandDispatcher(sys::ComponentContext* context,
                          ViewListener listener = nullptr)
      : context_(context), listener_(std::move(listener)){};

  // |fuchsia::virtualization::WaylandDispatcher|
  void OnNewConnection(zx::channel channel);

  fidl::InterfaceHandle<fuchsia::virtualization::WaylandDispatcher>
  NewBinding() {
    return bindings_.NewBinding();
  }

 private:
  fuchsia::sys::LauncherPtr ConnectToLauncher() const;

  void OnNewView(fidl::InterfaceHandle<fuchsia::ui::app::ViewProvider> view);
  void Reset(zx_status_t status);

  fuchsia::virtualization::WaylandDispatcher* GetOrStartBridge();

  sys::ComponentContext* context_;

  ViewListener listener_;
  fidl::Binding<fuchsia::virtualization::WaylandDispatcher> bindings_{this};
  fuchsia::sys::ComponentControllerPtr bridge_;
  fuchsia::virtualization::WaylandDispatcherPtr dispatcher_;
  fuchsia::wayland::ViewProducerPtr view_producer_;
};

};  // namespace guest

#endif  // LIB_VIRTUALIZATION_SCENIC_WAYLAND_DISPATCHER_H_
