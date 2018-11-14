// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_GUEST_SCENIC_WAYLAND_DISPATCHER_H_
#define LIB_GUEST_SCENIC_WAYLAND_DISPATCHER_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding.h>
#include <lib/zx/channel.h>

namespace guest {

// Provides a |WaylandDispatcher| that will create scenic views for each
// wayland shell surface.
//
// This class is not thread-safe.
class ScenicWaylandDispatcher : public fuchsia::guest::WaylandDispatcher {
 public:
  ScenicWaylandDispatcher(component::StartupContext* context)
      : context_(context){};

  // |fuchsia::guest::WaylandDispatcher|
  void OnNewConnection(zx::channel channel);

  fidl::InterfaceHandle<fuchsia::guest::WaylandDispatcher> NewBinding() {
    return bindings_.NewBinding();
  }

 private:
  void Reset(zx_status_t status);

  fuchsia::guest::WaylandDispatcher* GetOrStartBridge();

  fidl::Binding<fuchsia::guest::WaylandDispatcher> bindings_{this};
  component::StartupContext* context_;
  fuchsia::sys::ComponentControllerPtr bridge_;
  fuchsia::guest::WaylandDispatcherPtr dispatcher_;
};

};  // namespace guest

#endif  // LIB_GUEST_SCENIC_WAYLAND_DISPATCHER_H_
