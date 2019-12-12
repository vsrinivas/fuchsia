// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_
#define SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_

#include <fuchsia/netemul/guest/cpp/fidl.h>
#include <fuchsia/virtualization/cpp/fidl.h>

#include <map>
#include <string>

#include "src/virtualization/lib/guest_interaction/client/guest_interaction_service.h"

class GuestInfo {
 public:
  bool operator<(const GuestInfo& rhs) const {
    return (realm_id < rhs.realm_id) || (realm_id == rhs.realm_id && guest_cid < rhs.guest_cid);
  }
  bool operator==(const GuestInfo& rhs) const {
    return (realm_id == rhs.realm_id) && (guest_cid == rhs.guest_cid);
  }

  uint32_t realm_id;
  uint32_t guest_cid;
};

class GuestDiscoveryServiceImpl final : public fuchsia::netemul::guest::GuestDiscovery {
 public:
  GuestDiscoveryServiceImpl();
  void GetGuest(fidl::StringPtr realm_name, std::string guest_name,
                fidl::InterfaceRequest<fuchsia::netemul::guest::GuestInteraction>) override;

 private:
  std::map<GuestInfo, std::unique_ptr<FuchsiaGuestInteractionService>> guests_;
  std::unique_ptr<sys::ComponentContext> context_;
  async::Executor executor_;
  fit::scope scope_;
  fuchsia::virtualization::ManagerPtr manager_;
  fidl::BindingSet<fuchsia::netemul::guest::GuestDiscovery> bindings_;

  FXL_DISALLOW_COPY_ASSIGN_AND_MOVE(GuestDiscoveryServiceImpl);
};

#endif  // SRC_VIRTUALIZATION_LIB_GUEST_INTERACTION_CLIENT_GUEST_DISCOVERY_SERVICE_H_
