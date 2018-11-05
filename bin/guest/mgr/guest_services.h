// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GARNET_BIN_GUEST_MGR_GUEST_SERVICES_H_
#define GARNET_BIN_GUEST_MGR_GUEST_SERVICES_H_

#include "garnet/bin/guest/mgr/guest_vsock_endpoint.h"

#include <fuchsia/guest/vmm/cpp/fidl.h>
#include <fuchsia/sys/cpp/fidl.h>
#include <lib/svc/cpp/service_provider_bridge.h>

namespace guestmgr {

class GuestServices : public fuchsia::guest::vmm::LaunchInfoProvider {
 public:
  GuestServices(fuchsia::guest::LaunchInfo launch_info);

  fuchsia::sys::ServiceListPtr ServeDirectory();

 private:
  // |fuchsia::guest::vmm::LaunchInfoProvider|
  void GetLaunchInfo(GetLaunchInfoCallback callback) override;

  component::ServiceProviderBridge services_;
  fidl::Binding<fuchsia::guest::vmm::LaunchInfoProvider> binding_{this};
  fuchsia::guest::LaunchInfo launch_info_;
};

}  // namespace guestmgr

#endif  // GARNET_BIN_GUEST_MGR_GUEST_SERVICES_H_
