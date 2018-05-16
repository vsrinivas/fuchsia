// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
#define GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_

#include <component/cpp/fidl.h>
#include <guest/cpp/fidl.h>
#include <unordered_map>

#include "garnet/bin/guest/mgr/guest_holder.h"
#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"
#include "garnet/bin/guest/mgr/vsock_server.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace guestmgr {

// Per the virto-vsock spec, CID values 0 and 1 are reserved and CID 2 is used
// to address the host. We'll allocate CIDs linearly starting at 3 for each
// guest in the environment.
static constexpr uint32_t kFirstGuestCid = 3;

class GuestEnvironmentImpl : public guest::GuestEnvironment {
 public:
  GuestEnvironmentImpl(uint32_t id, const std::string& label,
                       component::ApplicationContext* context,
                       fidl::InterfaceRequest<guest::GuestEnvironment> request);
  ~GuestEnvironmentImpl() override;

  void AddBinding(fidl::InterfaceRequest<GuestEnvironment> request);

  fidl::VectorPtr<guest::GuestInfo> ListGuests();
  uint32_t id() const { return id_; }
  const std::string& label() const { return label_; }

  // Invoked once all bindings have been removed and this environment has been
  // orphaned.
  void set_unbound_handler(std::function<void()> handler);

 private:
  // |guest::GuestEnvironment|
  void LaunchGuest(guest::GuestLaunchInfo launch_info,
                   fidl::InterfaceRequest<guest::GuestController> controller,
                   LaunchGuestCallback callback) override;
  void ListGuests(ListGuestsCallback callback) override;
  void ConnectToGuest(
      uint32_t id,
      fidl::InterfaceRequest<guest::GuestController> controller) override;
  void GetHostSocketEndpoint(
      fidl::InterfaceRequest<guest::ManagedSocketEndpoint> endpoint) override;

  void CreateApplicationEnvironment(const std::string& label);

  const uint32_t id_;
  const std::string label_;

  component::ApplicationContext* context_;
  fidl::BindingSet<guest::GuestEnvironment> bindings_;

  component::ApplicationEnvironmentPtr env_;
  component::ApplicationEnvironmentControllerPtr env_controller_;
  component::ApplicationLauncherPtr app_launcher_;
  component::ServiceProviderBridge service_provider_bridge_;

  VsockServer socket_server_;
  HostVsockEndpoint host_socket_endpoint_;
  uint32_t next_guest_cid_ = kFirstGuestCid;
  std::unordered_map<uint32_t, std::unique_ptr<GuestHolder>> guests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestEnvironmentImpl);
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
