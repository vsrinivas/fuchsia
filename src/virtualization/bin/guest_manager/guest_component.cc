// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/virtualization/bin/guest_manager/guest_component.h"

GuestComponent::GuestComponent(const std::string& label,
                               std::unique_ptr<GuestVsockEndpoint> endpoint,
                               std::shared_ptr<sys::ServiceDirectory> services,
                               std::unique_ptr<GuestServices> guest_services,
                               fuchsia::sys::ComponentControllerPtr component_controller)
    : label_(label),
      endpoint_(std::move(endpoint)),
      services_(std::move(services)),
      guest_services_(std::move(guest_services)),
      component_controller_(std::move(component_controller)) {}

void GuestComponent::ConnectToInstance(
    fidl::InterfaceRequest<fuchsia::virtualization::Guest> request) {
  services_->Connect(std::move(request));
}

void GuestComponent::ConnectToBalloon(
    fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> request) {
  services_->Connect(std::move(request));
}
