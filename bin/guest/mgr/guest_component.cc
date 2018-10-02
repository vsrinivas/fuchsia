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
      component_controller_(std::move(component_controller)) {}

void GuestComponent::ConnectToInstance(
    fidl::InterfaceRequest<fuchsia::guest::InstanceController> request) {
  services_.ConnectToService(std::move(request));
}

void GuestComponent::ConnectToBalloon(
    fidl::InterfaceRequest<fuchsia::guest::BalloonController> request) {
  services_.ConnectToService(std::move(request));
}

}  // namespace guestmgr
