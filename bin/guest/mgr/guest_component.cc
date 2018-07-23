// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "garnet/bin/guest/mgr/guest_component.h"

namespace guestmgr {

GuestComponent::GuestComponent(
    const std::string& label, std::unique_ptr<GuestVsockEndpoint> endpoint,
    component::Services services,
    fuchsia::sys::ComponentControllerPtr component_controller)
    : label_(label),
      endpoint_(std::move(endpoint)),
      services_(std::move(services)),
      component_controller_(std::move(component_controller)) {
  services_.ConnectToService(guest_controller_.NewRequest());
}

void GuestComponent::AddBinding(
    fidl::InterfaceRequest<fuchsia::guest::GuestController> request) {
  bindings_.AddBinding(guest_controller_.get(), std::move(request));
}

}  // namespace guestmgr
