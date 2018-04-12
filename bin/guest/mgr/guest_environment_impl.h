// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
#define GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_

#include <fuchsia/cpp/component.h>
#include <fuchsia/cpp/guest.h>
#include <unordered_map>

#include "garnet/bin/guest/mgr/guest_holder.h"
#include "lib/app/cpp/application_context.h"
#include "lib/fidl/cpp/binding.h"
#include "lib/fxl/macros.h"
#include "lib/svc/cpp/service_provider_bridge.h"

namespace guestmgr {

class GuestEnvironmentImpl : public guest::GuestEnvironment {
 public:
  GuestEnvironmentImpl(component::ApplicationContext* context,
                       const std::string& label,
                       fidl::InterfaceRequest<guest::GuestEnvironment> request);
  ~GuestEnvironmentImpl() override;

  std::vector<GuestHolder*> guests() const;

 private:
  // |guest::GuestEnvironment|
  void LaunchGuest(
      guest::GuestLaunchInfo launch_info,
      fidl::InterfaceRequest<guest::GuestController> controller) override;
  void GetSocketEndpoint(
      fidl::InterfaceRequest<guest::SocketEndpoint> socket_endpoint) override;

  void CreateApplicationEnvironment(const std::string& label);

  component::ApplicationContext* context_;
  fidl::BindingSet<guest::GuestEnvironment> bindings_;

  component::ApplicationEnvironmentPtr env_;
  component::ApplicationEnvironmentControllerPtr env_controller_;
  component::ApplicationLauncherPtr app_launcher_;
  component::ServiceProviderBridge service_provider_bridge_;

  std::unordered_map<GuestHolder*, std::unique_ptr<GuestHolder>> guests_;

  FXL_DISALLOW_COPY_AND_ASSIGN(GuestEnvironmentImpl);
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_ENVIRONMENT_IMPL_H_
