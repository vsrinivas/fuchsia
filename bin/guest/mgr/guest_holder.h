// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_HOLDER_H_
#define GARNET_BIN_GUEST_MGR_GUEST_HOLDER_H_

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/guest.h>

#include "garnet/bin/guest/mgr/remote_vsock_endpoint.h"
#include "lib/fidl/cpp/binding_set.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/services.h"

namespace guestmgr {

// Maintains references to resources associated with a guest throughout the
// lifetime of the guest.
class GuestHolder {
 public:
  GuestHolder(uint32_t cid, std::string label,
              std::unique_ptr<RemoteVsockEndpoint> socket_endpoint,
              component::Services services,
              component::ApplicationControllerPtr application_controller);

  uint32_t cid() const { return cid_; }
  const std::string& label() const { return label_; }

  void AddBinding(fidl::InterfaceRequest<guest::GuestController> controller);

 private:
  const uint32_t cid_;
  const std::string label_;
  std::unique_ptr<RemoteVsockEndpoint> socket_endpoint_;
  component::Services guest_services_;
  component::ApplicationControllerPtr guest_app_controller_;
  guest::GuestControllerPtr guest_controller_;

  fidl::BindingSet<guest::GuestController> bindings_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestHolder);
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_HOLDER_H_
