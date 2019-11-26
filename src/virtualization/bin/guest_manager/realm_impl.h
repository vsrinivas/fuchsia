// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_REALM_IMPL_H_
#define SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_REALM_IMPL_H_

#include <fuchsia/sys/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>
#include <lib/fidl/cpp/binding_set.h>
#include <lib/fit/function.h>
#include <lib/sys/cpp/component_context.h>

#include <unordered_map>

#include "src/virtualization/bin/guest_manager/guest_component.h"
#include "src/virtualization/bin/guest_manager/host_vsock_endpoint.h"

// Per the virto-vsock spec, CID values 0 and 1 are reserved and CID 2 is used
// to address the host. We'll allocate CIDs linearly starting at 3 for each
// guest in the environment.
static constexpr uint32_t kFirstGuestCid = 3;

class RealmImpl : public fuchsia::virtualization::Realm {
 public:
  RealmImpl(uint32_t id, const std::string& label, sys::ComponentContext* context,
            fidl::InterfaceRequest<fuchsia::virtualization::Realm> request);

  RealmImpl(const RealmImpl&) = delete;
  RealmImpl& operator=(const RealmImpl&) = delete;

  uint32_t id() const { return id_; }
  const std::string& label() const { return label_; }
  // Invoked once all bindings have been removed and this environment has been
  // orphaned.
  void set_unbound_handler(fit::function<void()> handler);

  void AddBinding(fidl::InterfaceRequest<Realm> request);
  std::vector<fuchsia::virtualization::InstanceInfo> ListGuests();

 private:
  // |fuchsia::virtualization::Realm|
  void LaunchInstance(fuchsia::virtualization::LaunchInfo launch_info,
                      fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller,
                      LaunchInstanceCallback callback) override;
  void ListInstances(ListInstancesCallback callback) override;
  void ConnectToInstance(
      uint32_t id, fidl::InterfaceRequest<fuchsia::virtualization::Guest> controller) override;
  void ConnectToBalloon(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::virtualization::BalloonController> controller) override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::virtualization::HostVsockEndpoint> endpoint) override;

  void OnVsockShutdown(uint32_t src_cid, uint32_t src_port, uint32_t dst_cid, uint32_t dst_port);

  fuchsia::virtualization::GuestVsockAcceptor* GetAcceptor(uint32_t cid);

  const uint32_t id_;
  const std::string label_;

  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr launcher_;

  HostVsockEndpoint host_vsock_endpoint_;
  uint32_t next_guest_cid_ = kFirstGuestCid;
  std::unordered_map<uint32_t, std::unique_ptr<GuestComponent>> guests_;
  fidl::BindingSet<fuchsia::virtualization::Realm> bindings_;
};

#endif  // SRC_VIRTUALIZATION_BIN_GUEST_MANAGER_REALM_IMPL_H_
