// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_VMM_WAYLAND_DISPATCHER_IMPL_H_
#define GARNET_BIN_GUEST_VMM_WAYLAND_DISPATCHER_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/zx/channel.h>

// The |WaylandDispatcherImpl| receives requests to start a new client from the
// virtio-wl device.
//
// This implementation will lazily create a single bridge instance and forward
// all clients to that bridge. The bridge implementation is located at
// //garnet/bin/wayland/bridge.
class WaylandDispatcherImpl : public fuchsia::guest::WaylandDispatcher {
 public:
  WaylandDispatcherImpl(fuchsia::sys::Launcher* launcher)
      : launcher_(launcher) {}

  // |fuchsia::guest::WaylandDispatcher|
  void OnNewConnection(zx::channel channel);

 private:
  fuchsia::guest::WaylandDispatcher* GetOrStartBridgeLocked()
      __TA_REQUIRES(mutex_);

  std::mutex mutex_;
  fuchsia::sys::Launcher* launcher_;
  fuchsia::sys::ComponentControllerPtr bridge_ __TA_GUARDED(mutex_);
  fuchsia::guest::WaylandDispatcherPtr dispatcher_ __TA_GUARDED(mutex_);
};

#endif  // GARNET_BIN_GUEST_VMM_WAYLAND_DISPATCHER_IMPL_H_
