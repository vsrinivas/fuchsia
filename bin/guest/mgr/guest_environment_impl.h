// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
#define GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fxl/macros.h>
#include <lib/svc/cpp/service_provider_bridge.h>
#include <unordered_map>

#include "garnet/bin/guest/mgr/guest_component.h"
#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"

namespace guestmgr {

// Per the virto-vsock spec, CID values 0 and 1 are reserved and CID 2 is used
// to address the host. We'll allocate CIDs linearly starting at 3 for each
// guest in the environment.
static constexpr uint32_t kFirstGuestCid = 3;

class GuestEnvironmentImpl : public fuchsia::guest::GuestEnvironment {
 public:
  GuestEnvironmentImpl(
      uint32_t id, const std::string& label, component::StartupContext* context,
      fidl::InterfaceRequest<fuchsia::guest::GuestEnvironment> request);

  uint32_t id() const { return id_; }
  const std::string& label() const { return label_; }
  // Invoked once all bindings have been removed and this environment has been
  // orphaned.
  void set_unbound_handler(std::function<void()> handler);

  void AddBinding(fidl::InterfaceRequest<GuestEnvironment> request);
  fidl::VectorPtr<fuchsia::guest::GuestInfo> ListGuests();

 private:
  // |fuchsia::guest::GuestEnvironment|
  void LaunchGuest(
      fuchsia::guest::GuestLaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::guest::GuestController> controller,
      LaunchGuestCallback callback) override;
  void ListGuests(ListGuestsCallback callback) override;
  void ConnectToGuest(uint32_t id,
                      fidl::InterfaceRequest<fuchsia::guest::GuestController>
                          controller) override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint)
      override;

  fuchsia::guest::GuestVsockAcceptor* GetAcceptor(uint32_t cid);

  const uint32_t id_;
  const std::string label_;

  component::StartupContext* context_;
  fidl::BindingSet<fuchsia::guest::GuestEnvironment> bindings_;

  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr launcher_;
  component::ServiceProviderBridge service_provider_bridge_;

  HostVsockEndpoint host_vsock_endpoint_;
  uint32_t next_guest_cid_ = kFirstGuestCid;
  std::unordered_map<uint32_t, std::unique_ptr<GuestComponent>> guests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestEnvironmentImpl);
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
