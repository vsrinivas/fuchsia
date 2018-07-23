// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_COMPONENT_H_
#define GARNET_BIN_GUEST_MGR_GUEST_COMPONENT_H_

#include "garnet/bin/guest/mgr/guest_vsock_endpoint.h"

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/svc/cpp/services.h>

namespace guestmgr {

// Maintains references to resources associated with a guest throughout the
// lifetime of the guest.
class GuestComponent {
 public:
  GuestComponent(const std::string& label,
                 std::unique_ptr<GuestVsockEndpoint> endpoint,
                 component::Services services,
                 fuchsia::sys::ComponentControllerPtr component_controller);

  const std::string& label() const { return label_; }
  GuestVsockEndpoint* endpoint() const { return endpoint_.get(); }

  void AddBinding(
      fidl::InterfaceRequest<fuchsia::guest::GuestController> request);

 private:
  const std::string label_;
  std::unique_ptr<GuestVsockEndpoint> endpoint_;
  component::Services services_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
  fuchsia::guest::GuestControllerPtr guest_controller_;
  fidl::BindingSet<fuchsia::guest::GuestController> bindings_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_COMPONENT_H_
