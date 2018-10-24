// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_ENVIRONMENT_CONTROLLER_IMPL_H_
#define GARNET_BIN_GUEST_MGR_ENVIRONMENT_CONTROLLER_IMPL_H_

#include <unordered_map>

#include <fuchsia/guest/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/component/cpp/startup_context.h>
#include <lib/fidl/cpp/binding_set.h>
#include "garnet/bin/guest/mgr/guest_component.h"
#include "garnet/bin/guest/mgr/host_vsock_endpoint.h"

namespace guestmgr {

// Per the virto-vsock spec, CID values 0 and 1 are reserved and CID 2 is used
// to address the host. We'll allocate CIDs linearly starting at 3 for each
// guest in the environment.
static constexpr uint32_t kFirstGuestCid = 3;

class EnvironmentControllerImpl : public fuchsia::guest::EnvironmentController {
 public:
  EnvironmentControllerImpl(
      uint32_t id, const std::string& label, component::StartupContext* context,
      fidl::InterfaceRequest<fuchsia::guest::EnvironmentController> request);

  EnvironmentControllerImpl(const EnvironmentControllerImpl&) = delete;
  EnvironmentControllerImpl& operator=(const EnvironmentControllerImpl&) =
      delete;

  uint32_t id() const { return id_; }
  const std::string& label() const { return label_; }
  // Invoked once all bindings have been removed and this environment has been
  // orphaned.
  void set_unbound_handler(std::function<void()> handler);

  void AddBinding(fidl::InterfaceRequest<EnvironmentController> request);
  fidl::VectorPtr<fuchsia::guest::InstanceInfo> ListGuests();

 private:
  // |fuchsia::guest::EnvironmentController|
  void LaunchInstance(
      fuchsia::guest::LaunchInfo launch_info,
      fidl::InterfaceRequest<fuchsia::ui::viewsv1::ViewProvider> view_provider,
      fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller,
      LaunchInstanceCallback callback) override;
  void ListInstances(ListInstancesCallback callback) override;
  void ConnectToInstance(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::guest::InstanceController> controller)
      override;
  void ConnectToBalloon(
      uint32_t id,
      fidl::InterfaceRequest<fuchsia::guest::BalloonController> controller)
      override;
  void GetHostVsockEndpoint(
      fidl::InterfaceRequest<fuchsia::guest::HostVsockEndpoint> endpoint)
      override;

  fuchsia::guest::GuestVsockAcceptor* GetAcceptor(uint32_t cid);

  const uint32_t id_;
  const std::string label_;

  fuchsia::sys::EnvironmentPtr env_;
  fuchsia::sys::EnvironmentControllerPtr env_controller_;
  fuchsia::sys::LauncherPtr launcher_;

  HostVsockEndpoint host_vsock_endpoint_;
  uint32_t next_guest_cid_ = kFirstGuestCid;
  std::unordered_map<uint32_t, std::unique_ptr<GuestComponent>> guests_;
  fidl::BindingSet<fuchsia::guest::EnvironmentController> bindings_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_ENVIRONMENT_CONTROLLER_IMPL_H_
