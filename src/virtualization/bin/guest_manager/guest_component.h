// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_COMPONENT_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_COMPONENT_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/svc/cpp/services.h>

#include "src/virtualization/bin/guest_manager/guest_services.h"
#include "src/virtualization/bin/guest_manager/guest_vsock_endpoint.h"

// Maintains references to resources associated with a guest throughout the
// lifetime of the guest.
class GuestComponent {
 public:
  GuestComponent(const std::string& label,
                 std::unique_ptr<GuestVsockEndpoint> endpoint,
                 component::Services services,
                 std::unique_ptr<GuestServices> guest_services,
                 fuchsia::sys::ComponentControllerPtr component_controller);

  const std::string& label() const { return label_; }
  GuestVsockEndpoint* endpoint() const { return endpoint_.get(); }

  void ConnectToInstance(
      fidl::InterfaceRequest<fuchsia::guest::InstanceController> request);
  void ConnectToBalloon(
      fidl::InterfaceRequest<fuchsia::guest::BalloonController> request);

 private:
  const std::string label_;
  std::unique_ptr<GuestVsockEndpoint> endpoint_;
  component::Services services_;
  std::unique_ptr<GuestServices> guest_services_;
  fuchsia::sys::ComponentControllerPtr component_controller_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_GUEST_COMPONENT_H_
